#ifndef CONNECTION_H
#define CONNECTION_H

#include <atomic>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <string>
#include <unistd.h>

// Global shutdown flag to signal all threads to shutdown gracefully.
extern std::atomic<bool> should_shutdown;

// Structure for managing the connection (sending/receiving, disconnecting)
struct radio_connection{
    bool is_https;
    int socket_fd;

    SSL_CTX *ctx = nullptr;
    SSL *ssl = nullptr;

    ssize_t send_data(const std::string& data) {
        if(is_https){
            return SSL_write(ssl, data.c_str(), data.length());
        }
        else{
            return write(socket_fd, data.c_str(), data.length());
        }
    }

    ssize_t receive_data(char* buffer, size_t max_length) {
            if (is_https) {
                return SSL_read(ssl, buffer, max_length);
            } else {
                return read(socket_fd, buffer, max_length);
            }
        }

void disconnect() {
        if (is_https) {
            if (ssl) {
                SSL_free(ssl);
                ssl = nullptr;
            }
            if (ctx) {
                SSL_CTX_free(ctx);
                ctx = nullptr;
            }
        }
        if (socket_fd >= 0) {
            close(socket_fd);
            socket_fd = -1;
        }
    }
};


#endif // CONNECTION