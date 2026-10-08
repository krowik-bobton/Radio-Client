#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#include "radio_connection.h"
#include "err.h"
#include "parser.h"

// Function for connecting to the server based on the provided configuration.
// Returns the socket file descriptor of the connection.
// Throws FatalError if connection couldn't be established.
int connect_to_server(const client_configs& configs){
    addrinfo hints;
    std::memset(&hints, 0, sizeof(addrinfo));
    hints.ai_family = configs.ip_version; // set the IP_VERSION to the desired one.
    hints.ai_socktype = SOCK_STREAM;      // set the socket to TCP
    addrinfo *address_results;
    int errcode = getaddrinfo(configs.url_parsed.host.c_str(), configs.url_parsed.port.c_str(), &hints, &address_results);
    if (errcode != 0){
        throw SystemError(std::string("getaddrinfo: ") + gai_strerror(errcode));
    }

    int socket_fd = -1;

    // Try to connect to each of the addresses returned by getaddrinfo until one succeeds
    for(addrinfo *addr_ptr = address_results; addr_ptr != nullptr; addr_ptr = addr_ptr->ai_next){
        socket_fd = socket(addr_ptr->ai_family, addr_ptr->ai_socktype, addr_ptr->ai_protocol);

        if(socket_fd < 0) continue;

        if(connect(socket_fd, addr_ptr->ai_addr, addr_ptr->ai_addrlen) == 0){
            if (configs.verbosity >= 1) {
                char ip_str[INET6_ADDRSTRLEN];
                void *addr;
                if (addr_ptr->ai_family == AF_INET) {
                    struct sockaddr_in *ipv4 = (struct sockaddr_in *)addr_ptr->ai_addr;
                    addr = &(ipv4->sin_addr);
                } else {
                    struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)addr_ptr->ai_addr;
                    addr = &(ipv6->sin6_addr);
                }
                inet_ntop(addr_ptr->ai_family, addr, ip_str, sizeof(ip_str));
                
                if (addr_ptr->ai_family == AF_INET6) {
                    std::cerr << "connecting to server [" << ip_str << "]:" << configs.url_parsed.port << "\n";
                } else {
                    std::cerr << "connecting to server " << ip_str << ":" << configs.url_parsed.port << "\n";
                }
            }
            break; // Successfully connected
        }
        // If connection failed, close the socket and try the next address
        close(socket_fd);
        socket_fd = -1;
    }

    freeaddrinfo(address_results);

    if(socket_fd == -1){
        throw FatalError("Couldn't connect to the server");
    }

    // Set the socket timeout
    if (configs.timeout > 0){
        struct timeval tv;
        tv.tv_sec = configs.timeout / 1000; // Timeout is given in miliseconds
        tv.tv_usec = (configs.timeout % 1000) * 1000; // Microseconds

        if(setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0){
            close(socket_fd);
            throw SystemError("Failed to set socket timeout (setsockopt)");
        }
    }

    return socket_fd;
}

// Setup the connection based on the provided configuration.
// If the connection is HTTPS, also perform the SSL handshake.
// Returns the radio_connection structure containing the information about the connection.
// Throws FatalError if connection or SSL handshake couldn't be established.
// Throws SystemError if there was an error during the SSL setup.
radio_connection setup_connection(const client_configs& configs){
    radio_connection connection;
    connection.is_https = configs.url_parsed.is_https;
    connection.ctx = nullptr;
    connection.ssl = nullptr;

    connection.socket_fd = connect_to_server(configs);
    if(connection.socket_fd < 0){
        throw FatalError("Couldn't connect to the server");
    }

    if(connection.is_https){
        SSL_CTX *ctx = SSL_CTX_new(TLS_method());
        if(!ctx){
            close(connection.socket_fd);
            throw SystemError("SSL_CTX_new");
        }
        connection.ctx = ctx;

        SSL *ssl = SSL_new(connection.ctx);
        if(!ssl){
            close(connection.socket_fd);
            throw SystemError("SSL_new");
        }
        connection.ssl = ssl;

        SSL_CTX_set_default_verify_paths(connection.ctx);

        SSL_set_verify(connection.ssl, SSL_VERIFY_PEER, nullptr);

        if(SSL_set_fd(connection.ssl, connection.socket_fd) == 0){
            close(connection.socket_fd);
            throw SystemError("SSL_set_fd");
        }

        SSL_set_tlsext_host_name(connection.ssl, configs.url_parsed.host.c_str());

        int ret = SSL_connect(connection.ssl);
        if(ret <= 0){
            int ssl_err = SSL_get_error(connection.ssl, ret);
            std::string error_msg;
    
            if(ssl_err == SSL_ERROR_SSL) {
                unsigned long err_code = ERR_get_error();
                error_msg = ERR_error_string(err_code, nullptr);
            } else {
                error_msg = std::to_string(ssl_err);
            }
    
            connection.disconnect();
            connection.socket_fd = -1;
            throw FatalError("SSL_connect failed: " + error_msg);
        }
    }
    return connection;
}