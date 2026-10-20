# antor-engine

Antor is an MMO. This repository is the engine skeleton: an authoritative C++ server, an engine-agnostic C# client, and one FlatBuffers schema that generates both sides. There is no gameplay yet. The only message is a handshake.

```
protocol/antor.fbs          schema, the only hand-written protocol definition
server/                     C++20 simulation host; TCP is one transport
client/                     C# class libraries (no game-engine host)
docs/ARCHITECTURE.md        threading, ownership, networking, open decisions
docs/AI-WORLD.md            generative AI: proposals, budgets, director
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

The client is three class libraries plus the generated messages. There is no standalone game executable. A future Unity or Godot host calls `ClientConnection.ConnectAndHandshakeAsync`, which is a `FramedTransport` over a TCP socket. `HandshakeClient.ExchangeAsync` takes an `ITransport`, so a different transport does not change the handshake.

## Layout

| Path | Role |
| --- | --- |
| `protocol/antor.fbs` | `Hello` / `HelloAck` schema |
| `server/include` | Session bus, TCP transport, loopback transport, zone, codec |
| `server/src` | Sim thread, TCP worker, process entry |
| `server/tests` | C++ tests, including TCP and in-memory handshakes |
| `client/src/Antor.Protocol` | Unframed codec and the length-prefix helper |
| `client/src/Antor.Net` | `ITransport`, TCP socket, framed transport, handshake |
| `client/src/Antor.FlatBuffers` | C# FlatBuffers runtime from the pinned tag |
| `client/src/Antor.Protocol.Generated` | `flatc --csharp` output, not committed |

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the session boundary, why the running server still speaks TCP, and the decisions that are intentionally not made yet. [docs/AI-WORLD.md](docs/AI-WORLD.md) is the design for generative AI on top of that scaffold: the model proposes, the sim disposes, and a missed budget falls back to authored content.
