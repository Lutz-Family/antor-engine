# Architecture

Antor is authoritative: the server owns world state, and a client message is a request the server may accept or reject. This skeleton has no world. It exists to fix the process layout, the thread boundary, and the wire format before gameplay code has a place to hide shared mutable state.

The supported server platform for this version is Linux. The client is a set of C# class libraries with no engine host.

## Layout

- `protocol/antor.fbs` is the only hand-written description of a message. `flatc` generates C++ into the CMake build directory and C# into `client/src/Antor.Protocol.Generated/Generated` at build time. Generated files are not committed.
- `server/` is the simulation host. `antor-server` binds a TCP port, runs until SIGINT or SIGTERM, and joins its threads before exiting.
- `client/src/Antor.Protocol` frames bytes and turns them into handshake values. `client/src/Antor.Net` owns one TCP socket. Neither references Unity or Godot.

A session speaks one schema. The net thread does not interpret it. The sim thread does.

## Schema tool

FlatBuffers is the schema tool. The payload is a `Packet` table whose `body` union is either `Hello` or `HelloAck`, with file identifier `ANTR`.

FlatBuffers wins over protobuf for this codebase because a reader can walk the buffer in place. The C# client keeps one `FlatBufferBuilder`, one `Verifier`, and one rented scratch array on the connection and reuses them. The steady-state encode/decode path is covered by a test that allows zero bytes of GC allocation after warmup. Protobuf's C# API builds a message object graph per parse, which fights that constraint directly. Cap'n Proto has the same zero-copy shape but a weaker official C# generator.

The pin is FlatBuffers **25.12.19**. `flatc` is the official Linux g++-13 release binary. The C++ headers and the C# runtime are taken from that same tag. The 25.12.19 NuGet package was never published (upstream release issue), so depending on `Google.FlatBuffers` from NuGet would either lag the compiler or force an older `flatc`. Compiling the runtime from the tag keeps the generated `FLATBUFFERS_25_12_19()` check honest. Hashes for the tarball and the `flatc` zip live in `tools/fetch_flatbuffers.py`.

TCP framing is not part of the schema. Both sides prefix a payload with a little-endian `uint32` length. Empty payloads and payloads over 65536 bytes are rejected. v0 assumes a little-endian host; the server static-asserts that.

`protocol_version` is the constant `1` in the schema default, in `antor::protocol::kProtocolVersion`, and in `Antor.Protocol.ProtocolVersion.Current`. Bump all three together. There is no compatibility window yet.

## Networking

The server is the listener. The client never accepts a connection. The only implemented exchange is:

1. The client connects and sends `Hello { protocol_version, client_nonce }`.
2. The sim thread checks the version. On success it sends `HelloAck` echoing `client_nonce` and attaching a server nonce. On failure it drops the socket.
3. The client accepts the ack only when the version and the echoed nonce match.

The server nonce is a per-process counter starting at 1. It is not a key and not a secret. It only lets a test tell two acks apart.

**The session channel is TCP.** The handshake has to arrive, arrive once, and arrive in order. TCP already provides that, and v0 has no position stream that would benefit from an unreliable datagram. Accepted sockets set `TCP_NODELAY` so a small handshake is not held for Nagle. `SIGPIPE` is blocked and sends use `MSG_NOSIGNAL`, so a peer reset cannot kill the process.

Replication traffic is not on this socket's design yet. See open decisions.

The net thread is schema-agnostic. It accepts, reads, splits the byte stream into frames, and writes bytes the sim thread has already encoded. A bad length closes the connection before the payload is queued. A payload that fails FlatBuffers verification is closed by the sim thread, which is the only place that knows what a valid `Packet` is. Version mismatch is also a close, not an error message: the schema has no error variant.

Handshake bytes are applied on the sim tick, not on the read that received them. Worst-case accept-to-ack latency is about one tick (the default period is 50 ms). That is acceptable for a handshake and keeps a single schema interpreter.

## Threading

Three threads, with fixed jobs:

| Thread | Owns | Does not touch |
| --- | --- | --- |
| Main | Signal mask, `sigwait`, start and join | Sockets, zone, packet bytes |
| Net | Listen socket, accepted sockets, per-connection read/write buffers, frame reassembly | FlatBuffers, zone membership |
| Sim | `Zone`, session table, `InterestManager`, FlatBuffers `Codec`, tick clock | Socket file descriptors |

`main` blocks `SIGINT`, `SIGTERM`, and `SIGPIPE` before starting workers, so only `sigwait` on the main thread observes shutdown. Workers inherit the mask. Shutdown calls `request_stop` on both `std::jthread`s and joins the net thread first, then the sim thread. The net thread also watches an `eventfd` written from the stop callback so `poll` wakes immediately. The sim thread waits on a condition variable; the stop callback takes that wait mutex before notifying, which closes the lost-wakeup window. The mutex guards the wait only. No zone or socket state is stored under it.

Workers communicate through two bounded SPSC queues (`kQueueSlots` is 1024, so 1023 usable slots):

- `NetToSim`: `SessionOpened`, `SessionClosed`, `Packet`
- `SimToNet`: `Send`, `Disconnect`

Push moves the message into a reserved slot only after the queue is known to have room. A failed push leaves the caller's object intact. Each side keeps a private overflow (`optional` on a connection, `deque` on the sim) so a full queue preserves per-session order instead of blocking. Blocking both ways would deadlock when both queues are full. The sim drains at most 256 inbound messages per tick, then ticks anyway, so a burst cannot stall the clock for an unbounded time. When a connection cannot enqueue, the net thread stops reading it; TCP backpressure then applies to the peer.

The listen port is written on the main thread before either worker is started, then only read. That publish happens-before thread start. It is not a shared mutable variable afterwards.

The client library has no sim thread. `ClientConnection` is used by one caller, sequentially. The host (whatever it turns out to be) owns the thread the connection runs on.

## Simulation clock

The sim thread runs a fixed step. The default period is 50 ms, overridable with `--tick-ms`. Gameplay, when it exists, should see that nominal period even if the wall clock was late. `TickClock` implements the schedule:

- On time, or late by less than an extra period: sleep until the next deadline. The schedule does not drift.
- Later than that: drop the missed steps, and schedule the next tick one full period out. There is no catch-up burst.

`Zone::tick` records the nominal period, not the measured lateness. A dropped tick increments a counter logged at shutdown (`sim stopped ...`) and does not run extra steps.

## Zones, shards, interest

A process is one shard. `ServerConfig.shard_id` (default 1, `--shard`) is stored on the single `Zone` the sim thread creates. The zone id is 1. Sessions are `uint64` values assigned by the net thread; the zone stores those ids and never sees a socket.

`InterestManager::recipients` returns every session currently in the zone. Replication does not exist, so the sim thread calls it once per tick and ignores the result. The call is the seam a later snapshot step will use, and it allocates nothing.

Adding a session twice is a no-op. Removing a session that is not present is a no-op. The sim thread is the only writer.

## Memory ownership

**Server.** A `std::vector<std::byte>` in a queue message is owned by the producer until `try_push` succeeds, then by the consumer. Connection read and write buffers live and die with the connection object on the net thread. The FlatBuffers builder lives inside `Codec`, which the sim thread owns, and is cleared between messages rather than reallocated. The listen socket and the wake `eventfd` are owned by the net thread via move-only RAII. On shutdown the net thread drops its connections without waiting to deliver a matching `SessionClosed`; the sim thread is stopping too and discards its session table with the thread.

There is no `shared_ptr` graph between the threads. The queues are immovable members of `Server` and outlive both workers because the workers are joined in the destructor before the queues are destroyed.

**Client.** `ArrayPool<byte>.Shared` rents one receive buffer and one send buffer for the life of `ClientConnection`. `PacketCodec` rents one decode scratch buffer of `MaxPayloadLength` and returns it on dispose. `FlatBufferBuilder` keeps its internal buffer across `Clear()`. Decoding copies the payload into the scratch buffer and sets the FlatBuffers length to that payload's length. The copy is intentional: the stock `ByteBuffer` treats the array length as the readable length, and a rented array is larger than the frame. Reporting the true length stops the verifier from treating the next frame, or stale scratch bytes, as part of this one. That copy is bytes, not a managed object.

The codec test encodes and decodes 1000 hello/ack pairs after warmup and requires `GC.GetAllocatedBytesForCurrentThread` to stay flat. Socket completion is separate. Each in-flight send or receive uses a reused `SocketAsyncEventArgs` and a reused `ManualResetValueTaskSourceCore<int>` with `RunContinuationsAsynchronously` set, so the continuation is not run on the I/O thread. That post, and a cancellation registration when the token can be canceled, can allocate. Passing `CancellationToken.None` avoids the registration. The allocation budget above is the codec, which is the per-message path; the async wait is per socket operation and is called out here so it is not confused with the codec guarantee.

`ConfigureAwait(false)` is used on library awaits so a Unity-style synchronization context cannot deadlock a caller that blocks.

## Scalability assumptions

These are bounds for the skeleton, not a capacity plan and not measurements.

- One process is one shard. Zones do not share memory across processes. Cross-shard migration does not exist.
- One sim thread owns every zone in the process. Today that is one zone.
- One net thread serves every connection, using `poll`. The connection cap is 1024. Past that, extra accepts are closed.
- Outbound bytes queued on one connection are capped at 1 MiB, then the connection is closed.
- A player is in at most one zone, represented only as a session id so far.
- The tick must finish in a small fraction of the period. If it does not, ticks are dropped rather than queued as debt.
- The wire format is uncompressed and unencrypted.

`poll` plus a linear session lookup is the wrong shape past a few thousand connections. It is the right shape while the code is still small enough to read. The SPSC queues match one net thread and one sim thread; a second net thread would make the inbound queue wrong.

## Open decisions

These are deliberately not filled in.

1. **Replication transport.** TCP is the session channel. World state may later be unreliable UDP with a small reliability layer, QUIC, or more TCP. No choice is encoded, because there is no replicated state to judge it against.
2. **Interest algorithm.** Radius, grid, spatial hash, and rooms are all unchosen. The current policy is "everyone in the zone".
3. **Zone shape and handoff.** Zone size, how many zones a sim thread should own, and how a session moves between zones or shards are unchosen. The shard id is a label, not a router.
4. **Tick rate and catch-up.** 20 Hz and "drop missed ticks, pass the nominal dt" are placeholders. Gameplay may need a limited catch-up instead. That should be decided with a real system on the tick, not before.
5. **Authentication and encryption.** The handshake is not login. The socket is plaintext. TLS, DTLS, or a custom handshake are unchosen, as is the account identity that would replace a raw nonce.
6. **Protocol errors.** A bad version or a bad payload closes the TCP connection. There is no error message in the schema. Whether peers need a reason on the wire is open.
7. **Persistence.** No database, no save, no authoritative snapshot format.
8. **Entity model.** No ECS and no object hierarchy. Interest has nothing to filter until one exists.
9. **Client host.** Unity and Godot are both possible. The libraries target `net10.0` and stay on BCL types (`Socket`, `ArrayPool`, `Span`). A `netstandard2.1` build, or whatever the chosen host actually embeds, is deferred until that host is chosen. Do not take the current TFM as a promise that today's Unity will load the assembly.
10. **I/O multiplexing.** `poll` is the v0 demultiplexer. `epoll` or `io_uring` is the likely replacement, not a second `poll` thread.
11. **Queue fan-in.** Inbound is SPSC. Sharding the net thread requires an MPSC inbound queue or one queue per net thread. Not built.
12. **Buffer recycling.** Crossing the thread boundary moves a `std::vector`. A free-list return queue is the likely way to cut allocator traffic once packets are frequent. It is not worth the extra queue while the only message is a handshake.
13. **IPv6.** The server binds an IPv4 literal. The client opens an `InterNetwork` socket. Dual-stack and hostnames that resolve to IPv6 are unchosen. The client can still pass a DNS name that resolves to IPv4.
14. **Snapshot shape.** Full snapshots, deltas, and compression are unchosen. The 64 KiB frame cap is a safety rail, not a message-design target.
15. **Observability.** Shutdown writes one stderr line from the sim thread. There are no metrics, traces, or structured logs.
16. **Windows server.** POSIX sockets, `eventfd`, `sigwait`, and `/proc/self/fd` are assumed. A Windows host is a port, not a `#ifdef` sprinkled through this tree.
17. **Where the handshake runs.** It runs on the sim thread so there is one schema interpreter. Moving it to the net thread would cut the tick of latency. That trade is open once login does real work.
18. **Anti-cheat beyond authority.** "The server decides" is the whole policy. There is no further client-validation model.
