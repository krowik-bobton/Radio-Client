#include <string>
#include <vector>
#include <sstream>
#include <unistd.h> // optarg
#include <iostream>
#include <sys/socket.h>

#include "parser.h"
#include "err.h"
#include <numeric>

// Parse the string representing the url.
// Return a parsed_url structure.
parsed_url parse_url(const std::string& url){
    parsed_url result;

    std::string url_copy = url;

    // Check for the protocole, and remove it from the url_copy.
    // Set the default ports based on the protocol.
   if(url_copy.find("https://") == 0){
        result.is_https = true;
        result.port = DEFAULT_HTTPS_PORT;
        url_copy.erase(0, std::string("https://").length());
    } 
    else if (url_copy.find("http://") == 0){
        result.is_https = false;
        result.port = DEFAULT_HTTP_PORT;
        url_copy.erase(0, std::string("http://").length());
    }
    else{
        // No protocol provided, assume http.
        result.is_https = false;
        result.port = DEFAULT_HTTP_PORT;
    }

    // Extract the path if provided and erase it from the url_copy.
    // If no path is provided, the default path ("/") will be used.
    size_t path_position = url_copy.find("/");
    if(path_position != std::string::npos){
        result.path = url_copy.substr(path_position); 
        url_copy.erase(path_position);
    } 
    else {
        result.path = "/"; // default path
    }

    // Extract the port if provided and erase it from the url_copy.
    // If no port is provided, the default port will be used.
    size_t last_colon = url_copy.rfind(':');
    if (last_colon != std::string::npos && 
        (url_copy.find(']') == std::string::npos || last_colon > url_copy.find(']'))) {
        result.port = url_copy.substr(last_colon + 1);
        result.host = url_copy.substr(0, last_colon);
    } else {
        result.host = url_copy;
    }

    if (!result.host.empty() && result.host.front() == '[' && result.host.back() == ']') {
        result.host = result.host.substr(1, result.host.length() - 2);
        result.is_ipv6_literal = true;
    } else {
        result.is_ipv6_literal = false;
    }

    return result;
}

// Parse the arguments given by user
// and return client_configs structure.
// If multiple, overriding arguments are given the last one will be used.
client_configs parse(int argc, char *argv[]) {
    client_configs config;
    int opt;

    // If both ipv4 are provided, or none of them is provided:
    // ignore these requirements and set AF_UNSPEC
    bool ipv4_provided = false;
    bool ipv6_provided = false;

    while ((opt = getopt(argc, argv, "u:mqt:46v:")) != -1) {
        switch (opt) {
            case 'u':
                config.url_parsed = parse_url(optarg);
                break;
            case 'm':
                config.ask_for_metadata = true;
                break;
            case 'q':
                config.verbosity = 0;
                break;
            case 't': {
                try {   // std::stoi can throw an exception if the timeout has invalid format.
                    int timeout = std::stoi(optarg);
                    if(timeout < MIN_TIMEOUT || timeout > MAX_TIMEOUT){
                        throw FatalError("Timeout must be between "
                                 + std::to_string(MIN_TIMEOUT) + " and " + std::to_string(MAX_TIMEOUT));
                    }
                    config.timeout = timeout;
                }
                catch (const FatalError&) {
                    throw; // Propagate further
                }
                catch (const std::exception& e){
                    throw FatalError("Timeout must be a number!");
                }
                break;
            }
            case '4':
                ipv4_provided = true;
                config.ip_version = AF_INET;  // Only IPv4
                break;
            case '6':
                ipv6_provided = true;
                config.ip_version = AF_INET6;  // Only IPv6
                break;
            case 'v': {
                try {
                    int verbosity = std::stoi(optarg);
                    if(verbosity < MIN_VERBOSITY || verbosity > MAX_VERBOSITY){
                        throw FatalError("Verbosity must be between "
                            + std::to_string(MIN_VERBOSITY) + " and "
                            + std::to_string(MAX_VERBOSITY));
                    }
                    config.verbosity = verbosity;
                } catch(const FatalError&) {
                    throw;
                } catch(const std::exception& e){
                    throw FatalError("Invalid format of verbosity!");
                }
                break;
            }
            case '?': 
                throw FatalError(
                    std::string("Usage: ")
                    + argv[0] +
                    " -u <URL> [-m] [-q] [-v level] [-t timeout] [-4|-6]");
                break;
        }
    }

    if(ipv4_provided && ipv6_provided) {
        // If both ip requirements are requested, set AF_UNSPEC.
        config.ip_version = AF_UNSPEC;
    }

    if (config.url_parsed.host.empty()) {
        throw FatalError("-u flag with a radio address is mandatory!");
    }

    return config;
}

// Return the string representation of the client_configs.
std::string client_configs_to_string(const client_configs& configs){
    std::string result = 
                        "HOST: " + configs.url_parsed.host + "\n" +
                        "PORT: " + configs.url_parsed.port + "\n" +
                        "PATH: " + configs.url_parsed.path + "\n" +
                        "IS_HTTPS: " + (configs.url_parsed.is_https ? "YES" : "NO") + "\n" +
                        "------------------------------\n" +
                        "ask_for_metadata: " + (configs.ask_for_metadata ? "YES" : "NO") + "\n" +
                        "timeout: " + std::to_string(configs.timeout) + "\n" +
                        "verbosity: " + std::to_string(configs.verbosity) + "\n";
    if(configs.ip_version == AF_INET){
        result += "ip_version: IPv4\n"; 
    }
    else if(configs.ip_version == AF_INET6){
        result += "ip_version: IPv6\n";
    }
    else{
        result += "ip_version: ANY\n";
    }
    return result;
}