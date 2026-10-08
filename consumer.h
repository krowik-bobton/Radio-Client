#ifndef CONSUMER_H
#define CONSUMER_H

#include "shared_state.h"

// Body of the consumer thread.
// Takes characters from the shared state.audio_queue 
// and writes them on the standard output.
void audio_consumer(shared_state& state);

#endif // CONSUMER_H