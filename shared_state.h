#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <vector>

const size_t BUFFER_SIZE = 1024 * 64;       // Size of the buffer for reading from the socket
const size_t QUEUE_TARGET_SIZE = 1024 * 64; // Target size of the audio queue for prebuffering

// Safety limit for the audio queue (10 MB), if it is exceeded, 
// the producer thread will throw a FatalError and shutdown the client.
const size_t QUEUE_SAFETY_LIMIT = 1024 * 1024 * 10;

struct shared_state {
    // Audio queue
    std::mutex audio_mtx;
    std::queue<std::vector<char>> audio_queue;
    size_t current_buffer_size = 0;     // number of char values in audio_queue
    std::condition_variable audio_cv;   // Condition variable for synchronizing the
                                        // producer and consumer threads accessing the audio queue.

    // Stream lifecycle
    bool stream_active = true;
    bool timeout_occured = false;
    bool redirect_requested = false;

    std::atomic<bool> should_shutdown = false; // Flag to signal all threads to shutdown gracefully.
    std::atomic<int> global_socket_fd = -1;    // Socket file descriptor of the current connection.
};

#endif // SHARED_STATE_H