#ifndef PARSER_H
#define PARSER_H

#include <cstring>
#include <netdb.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

const std::string DEFAULT_HTTP_PORT = "80";
const std::string DEFAULT_HTTPS_PORT = "443";
const std::string DEFAULT_PATH = "/";

// Arguments limits and default values:
const int MAX_TIMEOUT = 100000; // in miliseconds
const int MIN_TIMEOUT = 100;
const int DEFAULT_TIMEOUT = 5000;

const int MAX_VERBOSITY = 4;
const int MIN_VERBOSITY = 0;
const int DEFAULT_VERBOSITY = 2;

struct parsed_url{
    bool is_https = false;
    std::string host;
    std::string port = DEFAULT_HTTP_PORT;
    std::string path = DEFAULT_PATH;
    bool is_ipv6_literal = false;
};

struct client_configs{
    parsed_url url_parsed;
    bool ask_for_metadata = false;      // Ask the server for metadata between chunks of audio bytes.
    int timeout = DEFAULT_TIMEOUT;      // Time
    int verbosity = DEFAULT_VERBOSITY;  // Level of verbosity
    int ip_version = AF_UNSPEC;         // Required IP version. By default, there are no requirements

};

// Parse the string representing the url.
// Return a parsed_url structure.
parsed_url parse_url(const std::string& url);

// Parse the arguments given by user
// and return client_configs structure.
// If multiple, overriding arguments are given the last one will be used.
client_configs parse(int argc, char *argv[]);

// Return te string representation of the client_configs.
std::string client_configs_to_string(const client_configs& configs);

#endif