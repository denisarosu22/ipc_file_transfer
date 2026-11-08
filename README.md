# IPC File Transfer

Homework for the Inter-process Communication lecture.
Rosu Denisa Florentina

## What it does

Two console programs in C++:
- `file_server` - waits for a connection and saves the received file in the `received` folder
- `file_client` - sends a file to the server (the file name is given as a command-line argument)

They communicate through TCP sockets, so they can run on the same PC (localhost).

## How it works

The client first sends a small header (a magic number, the length of the file name and the file size),
then the file name and the file content in chunks of 64 KB. Because TCP can send/receive the data
in smaller pieces, I read and write in a loop until all the bytes are transferred.
The server saves the data in a `.part` file, renames it when everything arrived and sends back a status byte (OK or error).

For resource management I used RAII:
- the `Socket` class closes the socket in its destructor
- `NetworkInit` calls `WSAStartup` / `WSACleanup`
- the result of `getaddrinfo` is freed with a `unique_ptr`
- if a transfer fails, the `.part` file is deleted automatically

All socket and file operations are checked, and errors are thrown as exceptions with the error code.
The exceptions are caught and logged in `main`, and on the server for every client,
so if one client fails the server keeps running.

## Build

I used Visual Studio 2022 with CMake (C++17, no extra libraries):
File -> Open -> Folder, then Build -> Build All.
The executables are in `out\build\x64-Debug\`.

Or from the command line:

```
cmake -S . -B build
cmake --build build
```

## Run

First start the server, then the client in another terminal:

```
.\out\build\x64-Debug\file_server.exe
.\out\build\x64-Debug\file_client.exe CMakeLists.txt
```

Optional arguments:
- `file_server [port] [output_dir]` (default 5555 and `received`)
- `file_client <file> [host] [port]` (default 127.0.0.1 and 5555)

The server is stopped with Ctrl+C.

## Testing

I tested sending a file on localhost, sending a file that doesn't exist, running the client
without arguments, running the client when the server is off and stopping the server with Ctrl+C.
In all cases the program showed the right message.