# Runtime acceleration: upstream-first Android port

Baseline: HulkPocket b048e92, observed RP6 execution reaches HalRegisterShutdownNotification (47); correction published and startup-layout CI passed. No rendered frame or gameplay yet. Upstream XboxXBERecomp is MIT, pinned to 767a81837fba2d86913f6af81a10b33661d35518. Preserve its copyright and license in reused source distributions.

## Strategy

Keep the proven ARM64 code cache, XBE loader, isolated execution diagnostic and guest memory/dispatcher. Do not rewrite generated game code, change compiler fingerprints, or invalidate cached objects without an explicit technical reason. Prefer importing and adapting coherent upstream modules rather than rebuilding individual exports. Retain strict failures for unsupported services; synthetic tests are not a substitute for actual game startup.

| Upstream module | Approach | Acceptance |
|---|---|---|
| kernel/crypto.cpp, rtl.cpp, ordinals.cpp | reuse portable logic, bridge guest memory and calling conventions; no keys embedded | known vectors, invalid buffers, ABI tests |
| kernel/memory.cpp, objects.cpp, threads.cpp, files.cpp | retain proven Android primitives; port missing semantic groups from reference, preserve path confinement | concurrency, timeout, object lifetime, memory aliases, disc tests |
| cpu/dispatch.cpp and xdk/hle.h | reuse tested CPU primitives; adapt named symbols and ABI mapping, continuation and HLE dispatch | direct/indirect/fastcall calls, original fallback, stack correctness |
| gpu/state.h, backend.h, texture.cpp | reuse state and conversion, guest memory bridge | known texture formats, swizzle, bounds, resource identity |
| gpu/vertex.cpp | replace DXGI format dependency with portable format descriptions | vertex/index topology and packed-format fixtures |
| xdk/d3d8.cpp | adapt HLE state transitions and game-specific symbol bindings before real renderer | synthetic draw command recording and state tests |
| gpu/shaders.cpp | preserve Xbox shader semantics; evaluate HLSL-to-SPIR-V or GLSL translation against fixtures | verified shader compilation and pixel comparisons |
| gpu/d3d12.cpp, render_thread.cpp | new Android backend behind existing Backend contract; replace Win32 and x86 SIMD | render clear/triangle/texture, depth, blending, copy, present and readback on device |
| xdk/dsound.cpp, dsp_image.cpp, audio/audio.h | reuse mixer and format decoding; replace XAudio2 with Android output | deterministic PCM/mix tests, real audio callback underrun measurements |
| core/input.cpp | map gamepad input to Android/SDL | RP6 buttons, analog sticks, triggers, reconnect |
| Android shell | integrate proven runtime and local game assets | APK lifecycle, storage, controls, frame pacing |

## Execution gates

1. Boot: complete startup-facing export groups and HLE bindings; log missing exports and actual call sequence. The 159 imports are an inventory, not a trace.
2. Rendering: use a recording backend for synthetic tests only, then real backend rendering. Never count no-op graphics functions as working. Select Vulkan or GLES based on tested shader and resource compatibility, not assumptions.
3. First game frame: original local game assets, real HLE draw/present; device validation with images and logs.
4. Gameplay: real input and audio, level loading, saves and lifecycle.
5. Stability: representative play sessions, thermal and frame pacing measurements, regression testing.

## Working discipline

Use batches organized by subsystem; run existing host and ARM64 CI plus module-specific tests. Compare against pinned upstream code. Keep the known-working diagnostic branch and cached recompilation pipeline intact. Test on RP6 only at meaningful integration gates or when actual guest behavior cannot be reproduced by synthetic fixtures. No commercial game code or proprietary firmware/keys in the repo. Keep diagnostic and playable builds clearly distinguished.
