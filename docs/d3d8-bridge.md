# D3D8 bridge checkpoint

The portable Xbox graphics bridge adapts state transitions from the pinned MIT XboxXBERecomp runtime/xdk/d3d8.cpp. It accepts explicit stdcall/fastcall/LTCG argument maps, validates guest pointers and stack cleanup, updates portable State and forwards clear/draw/present to an injected Backend. Tests use a recording backend and real CPU-side geometry assembly. No real frame is rendered.

Do not register these as strong guest replacements yet. The original game's generated image_symbols.h and ABI maps determine the correct names/locations. Guest CDevice mirrors, reference lifetimes, deferred state synchronization, pushbuffer walking, shader handle creation, swap rotation, and actual Android backend remain required. Unsupported calls fail, rather than returning fake success. A backend must consume borrowed vertex/index data synchronously or copy it before returning.

Next: audit game-specific symbol coverage locally, implement device mirrors and pushbuffer parsing, then use a real Android backend and bind strong guest overrides through the existing ELF weak aliases. The existing diagnostic executable and private code cache are unchanged.
