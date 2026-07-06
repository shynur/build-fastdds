# urpc2

`urpc2` is a small CMake project that wraps Fast DDS RPC into a direct C++
facade named `urpc2::Urpc2`. It is intentionally simple: users create one
endpoint with a unique name, register string-to-string handlers on it, and call
handlers on another named endpoint.

The request and response strings are opaque to the framework. Examples use
JSON-shaped text, but this project does not parse JSON or depend on a JSON
library.

## Public API

The public API is documented with Doxygen comments in
`include/urpc2.hpp`.

Important points:

- `Urpc2{name}` creates a DDS RPC service whose service name is `name`.
- Endpoint names must be unique in the DDS domain.
- Handler names are local to one endpoint.
- `register_handler()` stores or replaces one handler.
- `call(receiver_name, handler_name, args)` synchronously waits for the reply.

## Architecture

The IDL file is `src/types/processor.idl`:

```idl
module urpc2 {
    interface Processor {
        string router(in string handler_name, in string args);
    };
};
```

The generated Fast DDS RPC code lives in `src/types/`. The generated service has
one operation, `Processor::router()`. `Urpc2` uses that operation as a small
application-level router:

1. The caller passes a receiver endpoint name and handler name.
2. The generated client calls the receiver service's `router()` operation.
3. The receiver's generated server invokes `Urpc2::Impl::dispatch()`.
4. `dispatch()` looks up the handler name in the local handler registry.
5. The selected handler receives the opaque argument string and returns the
   opaque result string.

The current implementation in `src/urpc2.cpp` favors clarity over performance:

- Each `Urpc2` owns one server `DomainParticipant`.
- The server runs on one background thread.
- Each outgoing call creates a short-lived participant and generated client.
- A fixed discovery wait is used before sending the request.

This avoids long-lived generated client sharing while the first version is used
for correctness testing. A later version can replace this with cached clients
and explicit discovery/matching management.

## Build

Configure this project as a standalone CMake project. Point CMake at the Fast
DDS installation prefix:

```sh
cmake -S source/tests/urpc2 -B /tmp/urpc2-build \
  -G Ninja \
  -DCMAKE_PREFIX_PATH=/opt/install-x64 \
  -DCMAKE_CXX_COMPILER=clang++-6.0 \
  -DCMAKE_BUILD_TYPE=Release

cmake --build /tmp/urpc2-build -j2
```

The project builds:

- `urpc2_processor_gen`: static library for generated RPC code.
- `urpc2`: static wrapper library.
- `urpc2_simple`: one-process smoke example.
- `urpc2_mesh_node`: one-process mesh node used for multi-process tests.

## Tests

### One-process smoke test

`examples/simple.cpp` starts two endpoints in one process:

- `alice` registers `sub`.
- `bob` registers `add`.
- Each endpoint calls the other by endpoint name and handler name.

Run:

```sh
LD_LIBRARY_PATH=/opt/install-x64/lib:/opt/install-x64/lib64 \
  /tmp/urpc2-build/urpc2_simple
```

### Three-container mesh test

`examples/mesh_node.cpp` is the multi-process test driver. Start three
containers and run two node processes in each container:

- `urpc2-mesh-1`: `node1`, `node2`
- `urpc2-mesh-2`: `node3`, `node4`
- `urpc2-mesh-3`: `node5`, `node6`

Each process:

1. Creates `Urpc2{node_name}`.
2. Registers the same `echo` handler.
3. Waits for peers to start.
4. Runs multiple rounds.
5. Shuffles the five peer names each round.
6. Calls peers sequentially with a short randomized gap between calls.
7. Keeps serving briefly after finishing outgoing calls.

The important detail is that each process has one active caller loop, but all
six processes run concurrently. That creates an interleaved out-of-order RPC
stream without adding multiple client workers inside a process.

Example node command:

```sh
LD_LIBRARY_PATH=/opt/install-x64/lib:/opt/install-x64/lib64 \
  /work/urpc2-build/urpc2_mesh_node \
  node1 node1 node2 node3 node4 node5 node6 --rounds=3
```

Expected log shape for one node:

```text
READY node1 peers=5 rounds=3
ROUND node1 round=1
CALL node1 -> node4 round=1 call=1 attempt=1
OK node1 -> node4 round=1 call=1 attempt=1
...
DONE node1
```

For `--rounds=3`, each node should produce:

- `CALL`: 15
- `OK`: 15
- `DONE`: 1
- `RETRY`, `FAIL`, `ERROR`, `TERMINATE`: 0

Logs are written to `/work/mesh-logs/*.log` inside each container.

## Generated Files

Files under `src/types/` are generated from `processor.idl`. Do not hand-edit
them unless the generated output itself is being refreshed.
