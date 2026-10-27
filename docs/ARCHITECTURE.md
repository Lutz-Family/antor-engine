# Architecture

Antor is authoritative: the server owns world state, and a client message is a request the server may accept or reject. This skeleton has no world. It exists to fix the process layout, the thread boundary, and the wire format before gameplay code has a place to hide shared mutable state.

The supported server platform for this version is Linux. The client is a set of C# class libraries with no engine host.

## Layout

- `protocol/antor.fbs` is the only hand-written description of a message. `flatc` generates C++ into the CMake build directory and C# into `client/src/Antor.Protocol.Generated/Generated` at build time. Generated files are not committed.
- `server/` is the simulation host. `antor-server` installs the TCP transport, runs until SIGINT or SIGTERM, and joins its threads before exiting.
- `client/src/Antor.Protocol` turns handshake payloads into values and defines the length prefix. `client/src/Antor.Net` exposes `ITransport`. `FramedTransport` over `TcpBytePipe` is the TCP implementation. Neither references Unity or Godot.

A session speaks one schema. The transport worker does not interpret it. The sim thread does.

## Schema tool

FlatBuffers is the schema tool. The payload is a `Packet` table whose `body` union is either `Hello` or `HelloAck`, with file identifier `ANTR`.

FlatBuffers wins over protobuf for this codebase because a reader can walk the buffer in place. The C# client keeps one `FlatBufferBuilder`, one `Verifier`, and one rented scratch array on the connection and reuses them. The steady-state encode/decode path is covered by a test that allows zero bytes of GC allocation after warmup. Protobuf's C# API builds a message object graph per parse, which fights that constraint directly. Cap'n Proto has the same zero-copy shape but a weaker official C# generator.

The pin is FlatBuffers **25.12.19**. `flatc` is the official Linux g++-13 release binary. The C++ headers and the C# runtime are taken from that same tag. The 25.12.19 NuGet package was never published (upstream release issue), so depending on `Google.FlatBuffers` from NuGet would either lag the compiler or force an older `flatc`. Compiling the runtime from the tag keeps the generated `FLATBUFFERS_25_12_19()` check honest. Hashes for the tarball and the `flatc` zip live in `tools/fetch_flatbuffers.py`.

The length prefix is not part of the schema and not part of the session bus. Both TCP implementations prefix a payload with a little-endian `uint32` length: `antor::transport::tcp::write_frame` on the server and `FrameCodec` / `FramedTransport` on the client. Empty payloads and payloads over 65536 bytes are rejected. v0 assumes a little-endian host; the server static-asserts that.

`protocol_version` is the constant `1` in the schema default, in `antor::protocol::kProtocolVersion`, and in `Antor.Protocol.ProtocolVersion.Current`. Bump all three together. There is no compatibility window yet.

## Transport boundary

The sim thread and the protocol codec never see a socket, a demultiplexer, or a frame. They see sessions.

`SessionBus` is the sim-side type. The sim thread owns it.

- `try_pop` yields `SessionOpened`, `SessionClosed`, or `Packet`. A packet carries unframed FlatBuffers bytes and a `Channel`.
- `send(session, channel, bytes)` and `disconnect(session)` queue commands for the transport thread.

`Channel` is how a later transport arrives without an API change. It has a delivery and a sequence number:

| Delivery | Meaning today |
| --- | --- |
| `ReliableOrdered` | Lossless and in order for one session. Sequence is unused and stays 0. The handshake uses this. TCP implements it. |
| `UnreliableSequenced` | A future datagram worker may drop these and must not apply an older sequence after a newer one. Sequence is per session, chosen by the sender. No worker implements this delivery yet. |

The two directions are bounded SPSC queues (`kQueueSlots` is 1024, so 1023 usable slots):

- Inbound, transport to sim: `InboundEvent`
- Outbound, sim to transport: `OutboundCommand`

Push moves the message into a reserved slot only after the queue is known to have room. A failed push leaves the caller's object intact. `SessionBus` keeps a private overflow deque so a full outbound queue preserves order instead of blocking. The transport worker keeps its own per-connection overflow for the inbound direction. Blocking both ways would deadlock when both queues are full.

### Polymorphism

The packet path is monomorphic. `SessionBus::send` inlines to one SPSC push. `Channel` is a byte and an integer inside the queue message. There is no virtual call, no `std::function`, and no allocation beyond the payload `std::vector` the codec already built.

A virtual `send` on the sim thread was the wrong tool. The sim thread must not touch sockets, so the virtual function would only forward into the same queue. One callee means the vtable adds a call on every future replication message and removes nothing. Templating the sim on the transport type was the wrong tool for the same reason: the sim never names the transport, so a template would stamp the tick loop out once per worker without removing a cost.

The process picks a worker once, in `Server::start`. That call is not on the packet path. The client is a different shape, covered below: the host picks a transport at runtime, and the call is once per message, already across an async boundary.

### TCP worker

`TcpPollTransport` is the worker `Server::start` runs. `bind()` runs on the calling thread, so a failure is reported before either worker starts and the listen port is published before the transport thread runs. `run()` is that thread. It accepts with `poll`, splits the byte stream into frames, and is the only server code that handles file descriptors. Accepted sockets set `TCP_NODELAY`. `SIGPIPE` is blocked and sends use `MSG_NOSIGNAL`, so a peer reset cannot kill the process.

This worker implements `ReliableOrdered` only. Inbound packets are tagged `ReliableOrdered`. An outbound `send` with any other delivery closes that session, so the tag cannot quietly mean something else on TCP. The connection cap is 1024. Outbound bytes queued on one connection are capped at 1 MiB, then the connection closes. A read stops once one maximum frame is buffered, so one burst cannot grow the connection without bound.

### Loopback worker

`loopback::Endpoint` is the other worker. It has no sockets and no framing. The calling thread is the transport thread: it opens a session, delivers a payload, and reads the sim's commands. `test_loopback_handshake` runs the real sim thread against this endpoint. That is the evidence the bus is a swap point, not a comment. The TCP handshake test still drives `antor-server`, so the same sim path is covered with the real worker too.

### Handshake

1. The client connects and sends `Hello { protocol_version, client_nonce }` on `ReliableOrdered`.
2. The sim thread checks the version and the delivery. On success it sends `HelloAck` echoing `client_nonce` and attaching a server nonce. On failure it disconnects the session. The TCP worker turns that into a closed socket.
3. The client accepts the ack only when the delivery, the version, and the echoed nonce match.

The server nonce is a per-process counter starting at 1. It is not a key and not a secret. It only lets a test tell two acks apart.

TCP is the session channel `antor-server` speaks. The handshake has to arrive, arrive once, and arrive in order, and v0 has no position stream that would benefit from an unreliable datagram. Replication can later use `UnreliableSequenced` on a different worker. The sim call stays `SessionBus::send`.

The transport worker is schema-agnostic. It accepts, reads, and writes bytes the sim thread has already encoded. A bad length closes the connection before the payload is queued. A payload that fails FlatBuffers verification is closed by the sim thread, which is the only place that knows what a valid `Packet` is. Version mismatch is also a disconnect, not an error message: the schema has no error variant.

Handshake bytes are applied on the sim tick, not on the read that received them. Worst-case accept-to-ack latency is about one tick (the default period is 50 ms). That is acceptable for a handshake and keeps a single schema interpreter.

### Client

`ITransport` is the client boundary: `SendAsync(Channel, payload)` and `ReceiveAsync`. The payload is unframed FlatBuffers. `TransportPacket.Payload` stays valid until the next receive or dispose on that instance. Callers in this tree are single-threaded: one send and one receive in flight.

`IBytePipe` is raw bytes. `SendAsync` writes the whole buffer or throws. The buffer is a `Memory<byte>` because the socket completion args require a writable one; the pipe does not modify it. `ReceiveAsync` may be short; zero means the peer closed. `TcpBytePipe` is the socket and does not know about frames or `Channel`. `FramedTransport` sits on a pipe, adds the length prefix, and implements `ITransport` for `ReliableOrdered` only. A datagram transport implements `ITransport` directly and does not use `FramedTransport`.

`HandshakeClient.ExchangeAsync` takes an `ITransport`. `ClientConnection.ConnectAndHandshakeAsync` builds a `FramedTransport` over a `TcpBytePipe` and calls it. The loopback test calls the same method on an in-memory `ITransport`. A second test puts `FramedTransport` on an in-memory pipe, so the length prefix is shown to be separate from the socket.

The client interface is runtime polymorphism. A virtual call once per message is the cost of letting a host or a test pass a different transport without making the handshake generic. `FramedTransport.SendAsync` copies into a rented frame buffer and returns the pipe's `ValueTask`, so the frame layer does not allocate a second async state machine. An incomplete socket operation still allocates one state machine inside `TcpBytePipe`, the same cost the connection had before the split. The codec path is unchanged and remains the zero-GC test.

## Adding a transport

1. Write a worker that consumes `OutboundCommand` and produces `InboundEvent` on the two queues `Server` owns. Assign session ids. Leave `Zone`, `Codec`, `InterestManager`, and `sim/sim_thread.cpp` alone.
2. Implement the delivery you claim. `ReliableOrdered` is lossless and ordered per session. `UnreliableSequenced` may drop and must honor sequence. Close a session whose delivery you do not implement, so the tag cannot change meaning quietly. `TcpPollTransport::is_reliable_ordered` is the TCP form of that check.
3. Keep framing, encryption, and the demultiplexer inside the worker. The sim receives payload bytes.
4. Install the worker from `Server::start`, or from a test the way `loopback::Endpoint` is installed beside `run_sim`. On the client, implement `ITransport` (and `IBytePipe` only if the transport is a byte stream that still wants the length prefix).
5. A second transport thread is not a new worker plugged into the current queues. Those queues are SPSC. Several transport threads are the open fan-in decision, and that change stays behind `SessionBus`.

`server/include/antor/transport/loopback.hpp` is the smallest worker. `server/include/antor/transport/tcp/poll_transport.hpp` is the production one. `epoll`, `io_uring`, QUIC, or a reliability layer on UDP replace one of those, not the sim.

## Threading

Three threads, with fixed jobs:

| Thread | Owns | Does not touch |
| --- | --- | --- |
| Main | Signal mask, `sigwait`, start and join | Sockets, zone, packet bytes |
| Transport (TCP today) | Listen socket, accepted sockets, per-connection read/write buffers, frame reassembly, `poll` | FlatBuffers, zone membership |
| Sim | `Zone`, session table, `InterestManager`, FlatBuffers `Codec`, `SessionBus`, tick clock | Socket file descriptors, frames |

`main` blocks `SIGINT`, `SIGTERM`, and `SIGPIPE` before starting workers, so only `sigwait` on the main thread observes shutdown. Workers inherit the mask. Shutdown calls `request_stop` on both `std::jthread`s and joins the transport thread first, then the sim thread. The TCP worker watches an `eventfd` written from the stop callback so `poll` wakes immediately. The sim thread waits on a condition variable; the stop callback takes that wait mutex before notifying, which closes the lost-wakeup window. The mutex guards the wait only. No zone or socket state is stored under it.

The sim drains at most 256 inbound messages per tick, then ticks anyway, so a burst cannot stall the clock for an unbounded time. When a TCP connection cannot enqueue, the worker stops reading it; TCP backpressure then applies to the peer.

The listen port is written on the main thread before either worker is started, then only read. That publish happens-before thread start. It is not a shared mutable variable afterwards.

The client library has no sim thread. `HandshakeClient` and `ITransport` are used by one caller, sequentially. The host (whatever it turns out to be) owns the thread the connection runs on.

## Simulation clock

The sim thread runs a fixed step. The default period is 50 ms, overridable with `--tick-ms`. Gameplay, when it exists, should see that nominal period even if the wall clock was late. `TickClock` implements the schedule:

- On time, or late by less than an extra period: sleep until the next deadline. The schedule does not drift.
- Later than that: drop the missed steps, and schedule the next tick one full period out. There is no catch-up burst.

`Zone::tick` records the nominal period, not the measured lateness. A dropped tick increments a counter logged at shutdown (`sim stopped ...`) and does not run extra steps.

## Zones, shards, interest

A process is one shard. `ServerConfig.shard_id` (default 1, `--shard`) is stored on the single `Zone` the sim thread creates. The zone id is 1. Sessions are `uint64` values assigned by the transport worker; the zone stores those ids and never sees a socket. The TCP worker and the loopback worker both start at 1.

`InterestManager::recipients` returns every session currently in the zone. Replication does not exist, so the sim thread calls it once per tick and ignores the result. The call is the seam a later snapshot step will use, and it allocates nothing.

Adding a session twice is a no-op. Removing a session that is not present is a no-op. The sim thread is the only writer.

## Memory ownership

**Server.** A `std::vector<std::byte>` in a queue message is owned by the producer until `try_push` succeeds, then by the consumer. `Channel` is copied by value inside that message. Connection read and write buffers live and die with the connection object on the transport thread. The FlatBuffers builder lives inside `Codec`, which the sim thread owns, and is cleared between messages rather than reallocated. The listen socket and the wake `eventfd` are owned by `TcpPollTransport` via move-only RAII. `Server::start` moves that object onto the transport thread. On shutdown the TCP worker drops its connections without waiting to deliver a matching `SessionClosed`; the sim thread is stopping too and discards its session table with the thread.

`SessionBus`'s overflow deque is sim-thread private. The loopback endpoint holds no buffers of its own beyond the queue message it moves.

There is no `shared_ptr` graph between the threads. The queues are immovable members of `Server` and outlive both workers because the workers are joined in the destructor before the queues are destroyed. The loopback test keeps the same order: the sim thread is joined before the queues go out of scope.

**Client.** `FramedTransport` rents one receive buffer and one send buffer from `ArrayPool<byte>.Shared` for the life of the transport. `TcpBytePipe` does not rent those buffers and does not build frames. `PacketCodec` rents one decode scratch buffer of `MaxPayloadLength` and returns it on dispose. `FlatBufferBuilder` keeps its internal buffer across `Clear()`. `WriteHello` / `WriteHelloAck` copy an unframed payload into the caller-supplied span. Decoding copies that payload into the scratch buffer and sets the FlatBuffers length to that payload's length. The copy is intentional: the stock `ByteBuffer` treats the array length as the readable length, and a rented array is larger than the frame. Reporting the true length stops the verifier from treating the next frame, or stale scratch bytes, as part of this one. That copy is bytes, not a managed object.

The codec test encodes and decodes 1000 hello/ack pairs after warmup and requires `GC.GetAllocatedBytesForCurrentThread` to stay flat. Socket completion is separate. Each in-flight send or receive uses a reused `SocketAsyncEventArgs` and a reused `ManualResetValueTaskSourceCore<int>` with `RunContinuationsAsynchronously` set, so the continuation is not run on the I/O thread. That post, and a cancellation registration when the token can be canceled, can allocate. Passing `CancellationToken.None` avoids the registration. The allocation budget above is the codec, which is the per-message path; the async wait is per socket operation and is called out here so it is not confused with the codec guarantee. The in-memory test double allocates; it is not the hot path.

`ConfigureAwait(false)` is used on library awaits so a Unity-style synchronization context cannot deadlock a caller that blocks.

## Scalability assumptions

These are bounds for the skeleton, not a capacity plan and not measurements.

- One process is one shard. Zones do not share memory across processes. Cross-shard migration does not exist.
- One sim thread owns every zone in the process. Today that is one zone.
- One transport thread serves every connection. The TCP worker uses `poll`. The connection cap is 1024. Past that, extra accepts are closed.
- Outbound bytes queued on one connection are capped at 1 MiB, then the connection is closed.
- A player is in at most one zone, represented only as a session id so far.
- The tick must finish in a small fraction of the period. If it does not, ticks are dropped rather than queued as debt.
- The wire format is uncompressed and unencrypted.

`poll` plus a linear session lookup is the wrong shape past a few thousand connections. It is the right shape while the code is still small enough to read. The SPSC queues match one transport thread and one sim thread. A second transport thread would make the inbound queue wrong, which is a change behind `SessionBus`, not inside gameplay.

## Open decisions

These are deliberately not filled in.

1. **Replication transport.** The session bus already carries `ReliableOrdered` and `UnreliableSequenced`. TCP implements the first. Which worker should carry world state — unreliable UDP, QUIC, or more TCP — is still open, because there is still no replicated state to judge it against. Adding it is a new worker, not a sim edit. The `uint32` sequence is reserved so that call does not change. Reset rules, wrap, and how many in-flight unreliable packets a session may have are untested until that worker exists.
2. **Interest algorithm.** Radius, grid, spatial hash, and rooms are all unchosen. The current policy is "everyone in the zone".
3. **Zone shape and handoff.** Zone size, how many zones a sim thread should own, and how a session moves between zones or shards are unchosen. The shard id is a label, not a router.
4. **Tick rate and catch-up.** 20 Hz and "drop missed ticks, pass the nominal dt" are placeholders. Gameplay may need a limited catch-up instead. That should be decided with a real system on the tick, not before.
5. **Authentication and encryption.** The handshake is not login. The socket is plaintext. TLS, DTLS, or a custom handshake are unchosen, as is the account identity that would replace a raw nonce.
6. **Protocol errors.** A bad version or a bad payload disconnects the session. There is no error message in the schema. Whether peers need a reason on the wire is open.
7. **Persistence.** No database, no save, no authoritative snapshot format.
8. **Entity model.** No ECS and no object hierarchy. Interest has nothing to filter until one exists.
9. **Client host.** Unity and Godot are both possible. The libraries target `net10.0` and stay on BCL types (`Socket`, `ArrayPool`, `Span`). A `netstandard2.1` build, or whatever the chosen host actually embeds, is deferred until that host is chosen. Do not take the current TFM as a promise that today's Unity will load the assembly.
10. **I/O multiplexing.** `poll` lives inside `TcpPollTransport`. `epoll` or `io_uring` replaces that worker, or the body of it. The sim does not change.
11. **Queue fan-in.** `SessionBus::send` is the call that stays. Behind it, one SPSC inbound queue and one SPSC outbound queue match one transport thread. Several transport threads need an MPSC inbound queue or one queue per thread. Not built.
12. **Buffer recycling.** Crossing the thread boundary moves a `std::vector`. A free-list return queue is the likely way to cut allocator traffic once packets are frequent. It is not worth the extra queue while the only message is a handshake.
13. **IPv6.** The TCP worker binds an IPv4 literal. The client opens an `InterNetwork` socket. Dual-stack and hostnames that resolve to IPv6 are unchosen. The client can still pass a DNS name that resolves to IPv4.
14. **Snapshot shape.** Full snapshots, deltas, and compression are unchosen. The 64 KiB frame cap is a safety rail for the TCP byte stream, not a message-design target.
15. **Observability.** Shutdown writes one stderr line from the sim thread. There are no metrics, traces, or structured logs.
16. **Windows server.** POSIX sockets, `eventfd`, `sigwait`, and `/proc/self/fd` are assumed. A Windows host is a port, not a `#ifdef` sprinkled through this tree.
17. **Where the handshake runs.** It runs on the sim thread so there is one schema interpreter. Moving it to the transport thread would cut the tick of latency and would teach that thread the schema. That trade is open once login does real work.
18. **Anti-cheat beyond authority.** "The server decides" is the whole policy. There is no further client-validation model.
