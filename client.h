#ifndef CLIENT_H
#define CLIENT_H

#include "parser.h"
#include "radio_connection.h"

// Function for connecting to the server based on the provided configuration.
// Returns the socket file descriptor of the connection.
// Throws FatalError if connection couldn't be established.
int connect_to_server(const client_configs& configs);

// Setup the connection based on the provided configuration.
// If the connection is HTTPS, also perform the SSL handshake.
// Returns the radio_connection structure containing the information about the connection.
// Throws FatalError if connection or SSL handshake couldn't be established.
// Throws SystemError if there was an error during the SSL setup.
radio_connection setup_connection(const client_configs& configs);

#endif // CLIENT_H