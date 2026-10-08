#include <iostream>
#include <mutex>
#include <queue>
#include <vector>

#include "consumer.h"

// Body of the consumer thread.
// Takes characters from the shared state.audio_queue 
// and writes them on the standard output.
void audio_consumer(shared_state& state){
    bool is_prebuffering = true;

    while(true) {
        std::vector<char> chunk; // Chunk of audio bytes to write on the standard output
        {
            std::unique_lock<std::mutex> lock(state.audio_mtx);

            if (is_prebuffering){
                // Wait till the producer fills up the queue to the desired size 
                // or the stream is finished.
                // Prebuffering helps to avoid audio stuttering in the beginning of the stream.
                while (!(state.current_buffer_size >= QUEUE_TARGET_SIZE || !state.stream_active)) {
                     state.audio_cv.wait(lock);
                }

                if (state.current_buffer_size >= QUEUE_TARGET_SIZE) {
                    is_prebuffering = false;
                }
            } else {
                // Wait untill there is some audio data to write or the stream is finished.
                state.audio_cv.wait(lock, [&]{ return !state.audio_queue.empty() || !state.stream_active; });
            }

            // Break the loop if the stream is closed and there is no buffered audio
            if(state.audio_queue.empty() && !state.stream_active){
                break;
            }

            // Move the data from the shared queue to the local variable chunk.
            if(!state.audio_queue.empty()) {
                chunk = std::move(state.audio_queue.front());
                state.audio_queue.pop();
                state.current_buffer_size -= chunk.size();
            }
        } // release the mutex lock

        // Writing to standard output is outside of the critical section,
        // so it doesn't block the producer thread while writing.
        if (!chunk.empty()) {
            std::cout.write(chunk.data(), chunk.size());
            std::cout.flush();
        }
    }
}