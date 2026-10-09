# Radio-Client

A command-line **HTTP/HTTPS internet radio client** written in C++17 using POSIX sockets and OpenSSL. It connects to an Icecast/SHOUTcast-style server, writes the raw audio stream to standard output and, optionally, prints in-stream metadata (e.g. the current track title) to standard error.

Built as a university assignment (Computer Networks (Sieci Komputerowe) course, University of Warsaw).

## Features

- TCP connections over **IPv4 / IPv6** (`getaddrinfo`, tries each returned address until one succeeds); IPv6 literals in URLs (`http://[::1]:8000/`) are supported
- **HTTPS** via OpenSSL: TLS handshake.
- HTTP/1.1 request builder and response parser: status codes, headers, **redirects** (300/301/302/303/307/308) and **cookies** (`Set-Cookie` → `Cookie`)
- **ICY metadata** (`Icy-MetaData` / `icy-metaint`): metadata blocks are cut out of the audio stream and printed separately
- Receive **timeout** (`SO_RCVTIMEO`) with automatic reconnection from the original URL
- Multithreaded **producer–consumer** pipeline (`std::thread`, `std::mutex`, `std::condition_variable`) with **prebuffering** (64 KB) to avoid stuttering at the start, and a 10 MB safety limit on the buffer
- Graceful shutdown: typing `quit` flushes buffered audio and exits
- Custom exceptions (`FatalError`, `SystemError`) and 5 configurable verbosity levels

## Requirements

- Linux, `g++` with C++17 support, `make`
- OpenSSL development files (`libssl-dev` on Debian/Ubuntu)
- An external player that decodes the stream, e.g. `play` (SoX) or `mpv`

## Build

```bash
make        # builds ./sikradio
make clean  # removes object files and the executable
```

## Usage

```bash
./sikradio -u <url> [-m] [-t <timeout>] [-4] [-6] [-v <level>] [-q]
```

The client does **not** decode audio. Raw stream bytes go to `stdout`, so pipe them into a player. Diagnostics and metadata go to `stderr`.

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `-u url` | Stream URL (`http://` or `https://`; plain `host/path` is treated as HTTP). Default ports: 80 / 443. **Required.** | – |
| `-m` | Request metadata multiplexed with the audio (`Icy-MetaData: 1`); it is printed to `stderr`. | off |
| `-t timeout` | Milliseconds without incoming data after which the client reconnects. Range: 100–100000. | `5000` |
| `-4` | Force IPv4. | – |
| `-6` | Force IPv6. | – |
| `-v level` | Diagnostic verbosity, 0–4 (see below). | `2` |
| `-q` | Shorthand for `-v0`. | – |

Options may appear in any order, values may be attached to the flag (`-t3000`), and flags can be bundled (`-m46`). If `-4` and `-6` are both given, or neither, the IP version is chosen by `getaddrinfo`. If an option is repeated, the last occurrence wins.

### Verbosity levels

| Level | Output |
|-------|--------|
| 0 | nothing |
| 1 | communication with the server (resolved address, request, response headers, timeouts, connection closing) |
| 2 | critical errors (default) |
| 3 | non-critical warnings |
| 4 | debug messages (parsed configuration etc.) |

### Examples

Play a stream with `play` (SoX):

```bash
./sikradio -u http://stream.example.com:8000/radio.mp3 | play -q -t mp3 -
```

Play with `mpv` and print track titles to the terminal:

```bash
./sikradio -m -u https://stream.nowyswiat.online/mp3 | mpv --really-quiet -
```

Force IPv4, 3 s timeout, no diagnostics:

```bash
./sikradio -u http://stream.example.com:8000/radio.mp3 -4 -t3000 -q | play -q -t mp3 -
```

> The URLs above are examples. Public stream addresses change over time, so substitute any working MP3 Icecast/SHOUTcast URL.

### Exit codes

| Code | Situation |
|------|-----------|
| `0` | User typed `quit`, or the server closed the connection |
| `1` | Invalid arguments or a critical error |

On `quit` the client closes the connection and writes all data received so far before exiting.

## How it works

Three threads cooperate:

1. **Main (producer)** – connects (TCP, then TLS for HTTPS), sends `GET`, parses the response headers, follows redirects, then reads the body. Audio bytes are pushed to a shared queue; if `-m` is on, bytes are split according to `icy-metaint` into audio and metadata.
2. **Consumer** – waits until 64 KB are buffered, then continuously pops chunks from the queue and writes them to `stdout` outside the critical section, so it never blocks the producer.
3. **Input** – reads `stdin`; on `quit` it sets the shutdown flag and calls `shutdown()` on the active socket to interrupt the blocking read.

On a receive timeout, the client drops cookies and the redirect target and reconnects to the original URL.

## Project structure

| File | Purpose |
|------|---------|
| `main.cpp` | Entry point, request builder, thread setup, reconnect/redirect loop, `quit` handling |
| `parser.cpp/.h` | Command-line options (`getopt`) and URL parsing |
| `client.cpp/.h` | Socket connection (`getaddrinfo`, timeout) and TLS setup |
| `connection_handler.cpp/.h` | Response parsing: status, headers, cookies, redirects, ICY metadata, audio queueing |
| `consumer.cpp/.h` | Consumer thread writing audio to `stdout` with prebuffering |
| `radio_connection.h` | Plain-TCP / TLS connection wrapper (`send_data`, `receive_data`, `disconnect`) |
| `shared_state.h` | State shared between threads (queue, mutex, condition variable, flags) |
| `err.h` | `FatalError` and `SystemError` exceptions |
