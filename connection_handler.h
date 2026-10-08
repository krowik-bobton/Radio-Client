#ifndef CONECTION_HANDLER_H
#define CONNECTION_HANDLER_H

#include "shared_state.h"
#include "client.h"
#include "radio_connection.h"

// Function for processing the stream data received from the server.
// pushes the audio bytes to the shared audio_queue 
void serve_the_connection(radio_connection& connection, client_configs& configs, shared_state& state, std::vector<std::string>& cookies);

#endif // CONNECTION_HANDLER