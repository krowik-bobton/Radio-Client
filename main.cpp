#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "client.h"
#include "radio_connection.h"
#include "consumer.h"
#include "err.h"
#include "parser.h"
#include "shared_state.h"
#include "connection_handler.h"

bool be_quiet = false;
bool print_debug_lines = false;

// Return the sample request based on the provided configuration and cookies.
std::string sample_request(const client_configs &configs, const std::vector<std::string>& cookies){
    std::string host_header = configs.url_parsed.host;
    if (configs.url_parsed.is_ipv6_literal) {
        host_header = "[" + host_header + "]";
    }
    std::string request = 
        "GET " + configs.url_parsed.path + " HTTP/1.1\r\n" +
        "Host: " + host_header + "\r\n" +
        "Connection: Keep-Alive\r\n";

    for(const std::string& cookie : cookies){
        request += "Cookie: " + cookie + "\r\n";
    }

    if (configs.ask_for_metadata) {
        request += "Icy-MetaData: 1\r\n";
    }

    request += "\r\n";
    return request;
}

// Return the current timestamp in the format YYYY.MM.DD HH:MM:SS.
std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm* timeinfo = std::localtime(&time);
    
    char buffer[20];
    std::strftime(buffer, sizeof(buffer), "%Y.%m.%d %H.%M.%S", timeinfo);
    return std::string(buffer);
}



int main(int argc, char* argv[]){
    shared_state state;

    // Thread writing audio bytes to the standard output
    std::thread consumer_thread(audio_consumer, std::ref(state));

    // Thread reading user input from the standard input.
    // If user writes "quit\n", the program should shutdown gracefully (write all already buffered bytes and exit).
    std::thread input_thread([&]() {
        std::string line;
        while (std::getline(std::cin, line)) {
            // Check if line is exactly "quit" or ends with " quit" (space before quit)
            if (line == "quit" || 
                (line.size() >= 5 && line.substr(line.size() - 4) == "quit" && line[line.size() - 5] == ' ')) {
                if(print_debug_lines) std::cerr << "[DEBUG] Received quit command from the user" << std::endl;
                state.should_shutdown = true; 
                int current_fd = state.global_socket_fd.load();
                if (current_fd != -1) {
                    shutdown(current_fd, SHUT_RDWR);
                }
                break;
            }
        }
    });

    try {
        // Parse the input
        client_configs configs = parse(argc, argv);
        const client_configs original_configs = configs;
        be_quiet = original_configs.verbosity == 0 ? true : false;
        print_debug_lines = original_configs.verbosity == 4 ? true : false;

        if (configs.verbosity == 4) {
            std::cerr << "--- [DEBUG] CONFIGS ---\n" + client_configs_to_string(configs) + "--- [DEBUG] END OF CONFIGS --- \n\n"; 
        }

        std::vector<std::string> cookies;

        while(true) {
            if (state.should_shutdown) break;

            if(state.timeout_occured){
                if(configs.verbosity >= 1) std::cerr << "data receiving timeout" << std::endl;
                state.timeout_occured = false;
                state.redirect_requested = false;
                configs = original_configs;
                cookies.clear(); // clear the cookies
            }

            if (configs.verbosity >= 1) {
                std::cerr << get_timestamp() << "\nresolving name " << configs.url_parsed.host << std::endl;
            }


            // Establish the connection
            radio_connection connection = setup_connection(configs);

            // Update the global socket fd.
            state.global_socket_fd.store(connection.socket_fd);

            // Create the request
            std::string request = sample_request(configs, cookies);

            if(configs.verbosity >= 1) std::cerr << request << std::endl;

            // Send the request
            ssize_t bytes_sent = connection.send_data(request);
            if(bytes_sent < 0){
                connection.disconnect();
                throw SystemError("Failed to send the request to the server");
                break;
            }

            if (configs.verbosity == 4) {
                std::cerr << "[DEBUG] Successfully sent the request to " << configs.url_parsed.host << "\n";
            }

            // Serve the connection until shutdown, timeout or redirect
            serve_the_connection(connection, configs, state, cookies);

            // After processing the response, close the connection and reset the global socket fd.
            connection.disconnect();
            state.global_socket_fd.store(-1);

            // If no redirection and no timeout occured requested exit the loop.
            if ((!state.redirect_requested && !state.timeout_occured) || state.should_shutdown) {
                break;
            }

            // If redirection or timeout occured, the configs
            // are already updated in the serve_the_connection function,
            if(state.redirect_requested){
                if(configs.verbosity == 4) std::cerr << "[DEBUG] Redirecting to: " << configs.url_parsed.host << "\n";
                state.redirect_requested = false;
            }
        }

        {
            std::lock_guard<std::mutex> lock(state.audio_mtx);
            state.stream_active = false;
        }
        state.audio_cv.notify_all();
        consumer_thread.join();

        input_thread.detach();

        return 0;
    } catch (const FatalError& e) {
        if(!be_quiet) std::cerr << "FATAL ERROR: " << e.what() << "\n";
        // Signal all threads to shutdown gracefully
        state.should_shutdown = true;
        // Clean up
        {
            std::lock_guard<std::mutex> lock(state.audio_mtx);
            state.stream_active = false;
        }
        state.audio_cv.notify_all();
        if (consumer_thread.joinable()) consumer_thread.join();
        input_thread.detach();
        return 1;
    } catch (const SystemError& e) {
        if(!be_quiet) std::cerr << "SYSTEM ERROR: " << e.what() << "\n";
        state.should_shutdown = true;
        {
            std::lock_guard<std::mutex> lock(state.audio_mtx);
            state.stream_active = false;
        }
        state.audio_cv.notify_all();
        if (consumer_thread.joinable()) consumer_thread.join();
        input_thread.detach();
        return 1;
    } catch (const std::exception& e) {
        if(!be_quiet)std::cerr << "ERROR: " << e.what() << "\n";
        state.should_shutdown = true;
        {
            std::lock_guard<std::mutex> lock(state.audio_mtx);
            state.stream_active = false;
        }
        state.audio_cv.notify_all();
        if (consumer_thread.joinable()) consumer_thread.join();
        input_thread.detach();
        return 1;
    }
}