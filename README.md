# antor-engine

Antor is an MMO. This repository is the engine skeleton: an authoritative C++ server, an engine-agnostic C# client, and one FlatBuffers schema that generates both sides. There is no gameplay yet. The only message is a handshake.

```
protocol/antor.fbs          schema, the only hand-written protocol definition
server/                     C++20 simulation and TCP session host
client/                     C# class libraries (no game-engine host)
docs/ARCHITECTURE.md        threading, ownership, networking, open decisions
```

## Prerequisites

The server target is Linux.

- CMake 3.28 or newer, Ninja, and g++ with C++20 (the CMake presets select `g++`; CI uses g++ 13)
- Python 3, used to fetch the pinned FlatBuffers compiler and headers
- .NET SDK 10

The first configure or `dotnet test` downloads FlatBuffers 25.12.19 into `.deps/` and checks the hashes in `tools/fetch_flatbuffers.py`. That directory is gitignored.

## Server

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

./build/debug/antor-server --port 7777
```

Other presets: `release`, and `sanitizer` (Debug plus AddressSanitizer and UndefinedBehaviorSanitizer). `--port 0` asks the kernel for an ephemeral port. The process prints `listening <ipv4> <port>` and exits 0 on SIGINT or SIGTERM.

```bash
cmake --preset sanitizer
cmake --build --preset sanitizer
ctest --preset sanitizer
```

## Client

```bash
cd client
dotnet test --configuration Release
```

`dotnet test` generates the C# messages, builds the libraries, and runs the unit tests. The handshake test starts `antor-server` when it can find a binary at `build/{sanitizer,debug,release}/antor-server`, or when `ANTOR_SERVER_BIN` is set.

```bash
ANTOR_SERVER_BIN=$PWD/../build/debug/antor-server dotnet test --configuration Release
```

The client is three class libraries plus the generated messages. There is no standalone game executable: a future Unity or Godot host is expected to call `ClientConnection.ConnectAndHandshakeAsync`.

## Layout

| Path | Role |
| --- | --- |
| `protocol/antor.fbs` | `Hello` / `HelloAck` schema |
| `server/include` | Public server types: queues, frames, zone, interest, codec |
| `server/src` | Net thread, sim thread, process entry |
| `server/tests` | C++ tests, including an in-process handshake |
| `client/src/Antor.Protocol` | Framing and the reused FlatBuffers codec |
| `client/src/Antor.Net` | TCP connection and handshake |
| `client/src/Antor.FlatBuffers` | C# FlatBuffers runtime from the pinned tag |
| `client/src/Antor.Protocol.Generated` | `flatc --csharp` output, not committed |

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the threading model, why the session channel is TCP, and the decisions that are intentionally not made yet.
