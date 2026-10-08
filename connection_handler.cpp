#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "err.h"
#include "parser.h"
#include "connection_handler.h"

// Structure for managing the state of metadata processing.
struct metaint_state {
    size_t icy_metaint = 0;           // Interval of audio bytes between metadata.
    size_t audio_bytes_left = 0;      // Audio bytes left to read until the next metadata chunk.
    size_t meta_bytes_left = 0;       // Bytes of metadata left to read in the current metadata chunk.
    bool expect_meta_length = false;  // Is the next byte expected to be the length of the metadata chunk.
    std::string metadata_buffer = ""; // Buffer for metadata chunk.
};

// Function for processing the stream data received from the server.
// It extracts the metadata if it is requested and supported by the server,
// and pushes the audio bytes to the shared audio_queue
static void process_bytes_stream(const char* data, size_t length, client_configs& configs, shared_state& state, metaint_state& meta) {
    size_t offset = 0;

    while (offset < length) {
        if (configs.ask_for_metadata && meta.icy_metaint > 0) {
            // If metadata is requested and server supports it.
            if (meta.audio_bytes_left > 0) {
                // There are some audio bytes remaining before the metadata chunk, push them to the queue.
                size_t to_read = std::min(meta.audio_bytes_left, length - offset);
                std::vector<char> chunk(data + offset, data + offset + to_read);
                {
                    std::lock_guard<std::mutex> lock(state.audio_mtx);

                    if (state.current_buffer_size > QUEUE_SAFETY_LIMIT) {
                        throw FatalError("Buffer overloaded (more than 10MB in the buffer!)");
                    }

                    state.audio_queue.push(std::move(chunk));
                    state.current_buffer_size += to_read;
                }
                // Notify the consume thread that there are new audio bytes in the queue.
                state.audio_cv.notify_one();

                meta.audio_bytes_left -= to_read;
                offset += to_read;

                // If there are no more audio bytes left until the next metadata chunk,
                // set the flag to expect the metadata length byte.
                if (meta.audio_bytes_left == 0) {
                    meta.expect_meta_length = true; 
                }
            } 
            else if (meta.expect_meta_length) {
                // Current byte is the number of metadata bytes multiplied by 16
                unsigned char meta_length_byte = data[offset];
                meta.meta_bytes_left = meta_length_byte * 16; 

                meta.expect_meta_length = false;
                meta.metadata_buffer.clear();
                offset++; 

                if (meta.meta_bytes_left == 0) {
                    meta.audio_bytes_left = meta.icy_metaint; 
                }
            }
            else if (meta.meta_bytes_left > 0) {
                // Read the metadata content
                size_t to_read = std::min(meta.meta_bytes_left, length - offset);
                meta.metadata_buffer.append(data + offset, to_read);

                meta.meta_bytes_left -= to_read;
                offset += to_read;
                
                // Erase the sequence of null characters from the end of the metadata buffer.
                if (meta.meta_bytes_left == 0) {
                    size_t end = meta.metadata_buffer.find('\0');
                    std::string trimmed = (end != std::string::npos) 
                        ? meta.metadata_buffer.substr(0, end)
                        : meta.metadata_buffer;
                    
                    if (!trimmed.empty()) {
                        // Print the metadata
                        std::cerr << trimmed << "\n";
                    }
                    meta.audio_bytes_left = meta.icy_metaint;
                }            
            }
        } else { // No metadata.
            size_t to_read = length - offset;
            std::vector<char> chunk(data + offset, data + offset + to_read);
            {
                std::lock_guard<std::mutex> lock(state.audio_mtx);

                if (state.current_buffer_size > QUEUE_SAFETY_LIMIT) {
                    throw FatalError("Buffer overloaded (more than 10MB in the buffer!)");
                }

                state.audio_queue.push(std::move(chunk));
                state.current_buffer_size += to_read;
            }
            state.audio_cv.notify_one();
            offset += to_read;
        }
    }
}


// Function for serving the connection with the server. 
// It reads the data from the server and processes it until the stream is active.
void serve_the_connection(radio_connection& connection, client_configs& configs, shared_state& state, std::vector<std::string>& cookies) {
    char buffer[BUFFER_SIZE];   // Buffer for reading the data from the server
    bool headers_finished = false;
    std::string headers = "";   // Text value from the beginning of the response to the first empty line.
    metaint_state meta;

    while(true){
        if (state.should_shutdown) break;
        ssize_t bytes_read = connection.receive_data(buffer, BUFFER_SIZE);
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                state.timeout_occured = true;
                break;
            } 
            else {
                throw SystemError("receive_data failed: " + std::string(strerror(errno)));
            }
        } 
        
        if (bytes_read == 0) {
            if (!headers_finished) {
                throw FatalError("Server closed the connection before sending all headers!");
            }
            // Closing the connection is considered as a part of communication with the server
            // Print the message if the verbosity is greater or equal to 1.
            if(configs.verbosity >= 1 && state.should_shutdown == true) {
                std::cerr << "Connection closed by the user" << std::endl;
            }
            else if(configs.verbosity >= 1) {
                std::cerr << "Server closed the connection (code: " << errno << ")" << std::endl;
            }
            break;
        }                

        if(!headers_finished){
            // Append the entire buffer to the headers.
            // After finding the end of the headers, the remaining bytes will be considered as audio bytes and printed.
            headers.append(buffer, bytes_read);
            size_t end_of_headers = headers.find("\r\n\r\n");
            
            if(end_of_headers != std::string::npos){
                headers_finished = true;

                // HTTP headers are case insensitive.
                std::string headers_lowercase = headers.substr(0, end_of_headers);
                std::transform(headers_lowercase.begin(), headers_lowercase.end(), headers_lowercase.begin(), ::tolower);

                // Extract the 'status line' from the headers
                std::string status_line = headers.substr(0, headers.find("\r\n"));
                std::stringstream ss(status_line);
                std::string protocol = "";
                int status_code = -1;
                std::string status_message = "";
                
                // Protocol and status_code can't contain blank spaces, but status message can
                ss >> protocol >> status_code;
                std::getline(ss, status_message);
                
                // Extract the cookies. There can be many cookies passed by the server, extract them all and 
                // save them in std::vector<std::string> cookies
                size_t pos = 0;
                while (true) {
                    size_t cookie_pos = headers_lowercase.find("set-cookie:", pos);

                    if (cookie_pos == std::string::npos) break; // Didn't find any cookies.

                    // Delete irrelevant cookies attributes.
                    size_t end_of_line = headers.find("\r\n", cookie_pos);
                    if (end_of_line != std::string::npos) {
                        std::string cookie = headers.substr(
                            cookie_pos + std::string("Set-Cookie:").length(),
                            end_of_line - cookie_pos - std::string("Set-Cookie:").length());
                        
                        // Erase whitespaces from the beginning of the cookie string
                        size_t first_char = cookie.find_first_not_of(" ");
                        if (first_char != std::string::npos) cookie = cookie.substr(first_char);

                        size_t semicolon_pos = cookie.find(";");
                        if (semicolon_pos != std::string::npos) {
                            cookie.erase(semicolon_pos); 
                        }

                        // If a cookie with the same name exits, overwrite it.
                        std::string cookie_name = cookie.substr(0, cookie.find('='));
                        cookies.erase(std::remove_if(cookies.begin(), cookies.end(), [&](const std::string& c) {
                            return c.substr(0, c.find('=')) == cookie_name;
                        }), cookies.end());
                        cookies.push_back(cookie);

                        pos = end_of_line;
                        
                    } else {
                        break; 
                    }
                }

                // Print the headers
                if(configs.verbosity >= 1) {
                    std::cerr << headers.substr(0, headers.find("\r\n\r\n")) << std::endl << std::endl << std::endl;
                }

                // Interpreting the status codes
                if (status_code >= 400 || status_code < 0) {
                    throw FatalError("Fatal error status code in the response: "
                             + std::to_string(status_code) + " " + status_message);
                }

                if (status_code == 200) {                    
                    // Extract the icy-metaint value from the header
                    if(configs.ask_for_metadata){
                        size_t metaint_pos = headers_lowercase.find("icy-metaint:");

                        if(metaint_pos != std::string::npos){
                            size_t metadata_chunk_beginning = metaint_pos + std::string("icy-metaint:").length();
                            size_t metadata_chunk_end = headers.find("\r\n", metadata_chunk_beginning);

                            if(metadata_chunk_end != std::string::npos){
                                std::string metadata_content = headers.substr(metadata_chunk_beginning, metadata_chunk_end - metadata_chunk_beginning);
                                // Strip the leading whitespaces
                                size_t first_non_empty = metadata_content.find_first_not_of(" \t");
                                if(first_non_empty != std::string::npos) metadata_content = metadata_content.substr(first_non_empty);

                                try {
                                    meta.icy_metaint = std::stoull(metadata_content);
                                    meta.audio_bytes_left = meta.icy_metaint;
                                    if(configs.verbosity == 4) {
                                        std::cerr << "[DEBUG] Extracted interval between metadata: " << meta.icy_metaint << " bytes" << std::endl;
                                    }
                                } catch (...){
                                    if(configs.verbosity >= 3){ // Non critical 
                                        std::cerr << "[WARNING]: stoull failed to interpret the metadata_content: '" 
                                                    + metadata_content << + "'" << std::endl;
                                    }
                                }
                            }
                        }
                    }

                    // Print the buffer bytes that come after the end of the headers (previously appended to the headers)
                    // and consider them as audio bytes. 
                    size_t header_bytes_length = end_of_headers + std::string("\r\n\r\n").length(); 
                    size_t audio_bytes_in_buffer = headers.length() - header_bytes_length;

                    if (audio_bytes_in_buffer > 0) {
                        process_bytes_stream(headers.data() + header_bytes_length, audio_bytes_in_buffer, configs, state, meta);
                    }

                } 
                else if (status_code == 300 || status_code == 301 || status_code == 302 || status_code == 307 || status_code == 308 || status_code == 303) {
                    
                    // Extract the new address - location
                    size_t location_pos = headers_lowercase.find("location:");
                    
                    if (location_pos != std::string::npos) {
                        size_t adr_start = location_pos + std::string("Location:").length();
                        // Skip whitespaces
                        while (adr_start < headers.length() && headers[adr_start] == ' ') adr_start++;
                        size_t adr_end = headers.find("\r\n", adr_start);
                        
                        std::string new_url = headers.substr(adr_start, adr_end - adr_start);
                        
                        // Insert the new url_parsed structure in the place of the old one.
                        configs.url_parsed = parse_url(new_url);
                        
                        state.redirect_requested = true;
                        break;
                    }
                    else {
                        throw FatalError("Redirection status code received, but no Location header found!");
                    }
                } 
                else {
                    throw FatalError("Received unexpected status code: " + std::to_string(status_code) + " " + status_message);
                }
                headers.clear();

            }
        } else {
            // If headers_finished == true, then received bytes are considered as audio bytes
            process_bytes_stream(buffer, bytes_read, configs, state, meta);
        }
    } // End of the loop reading the response
}