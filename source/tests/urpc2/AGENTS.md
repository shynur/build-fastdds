# AGENTS.md

This directory is a standalone CMake project for the `urpc2` RPC-over-DDS
wrapper used by tests.

Use these documentation locations instead of repeating the same explanations:

- Public user API: `include/urpc2.hpp`
  - Doxygen comments on `urpc2::Urpc2`, `Handler`, `register_handler()`, and
    `call()`.
- Architecture and test flow: `README.md`
  - Covers IDL routing, generated code boundaries, build commands, the
    one-process smoke test, and the three-container mesh test.
- Internal implementation notes: `src/urpc2.cpp`
  - Ordinary block comments describe participant lifetime, server startup,
    outgoing call flow, discovery wait, and handler dispatch.
- Test-driver logic: `examples/simple.cpp` and `examples/mesh_node.cpp`
  - Comments describe the smoke test and the single-worker randomized mesh
    traffic model.
- Generated Fast DDS files: `src/types/`
  - Treat these as generated artifacts from `src/types/processor.idl`.

When changing this project:

- Update `include/urpc2.hpp` Doxygen for public API changes.
- Update `README.md` for architecture, build, or test-process changes.
- Prefer comments in the relevant implementation file over repeating long
  architecture explanations elsewhere.
- Keep CI integration out of this directory unless the user explicitly asks for
  it.
