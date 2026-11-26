# Vulkan reuse experiment

Base: aad3e38a4e2ede09318646048e389ab08a430bf6 (all five existing checks successful). No changes to experiment-runtime-integration or APK/backend selection. No disc data included.

Reference pins: [SCDA Android] (https://github.com/mogeldev/SCDA-XBERecomp-android/tree/a81808004c397cbee282031933cdda2972ffad4e), MIT; [Hulk Windows](https://github.com/lookingcoding/hulk-ultimate-destruction-recomp-xbox-ver-/tree/6603e8a4021a06331f63310ce47b42315d9dbfc6), MIT. SCDA Vulkan supports generated HLSL via glslang, native Vulkan resources and frame synchronization. Its backend ABI differs: Present(front,pillarbox) vs Present(front); Copy lacks Hulk scaling parameters, and the older shaders/state have different constant-buffer layouts. Keep guest memory bounds, current texture decode and runtime ownership. Do not replace entire state headers with older versions or use unchecked guest pointers. Def Jam repository license for the full Android-specific backend was not established, so no code imported from it.

Stage 1: import actual reusable Vulkan resource helpers and matched reference shader test under third_party/scda-vulkan. Build original regression suite separately, compile current Hulk HLSL with SCDA's binding/location conversion. Test real observed packed normal and diffuse declarations plus synthetic lights and two-stage combiner. Validate SPIR-V. A compiler mismatch is a blocker to fix, not a fallback to silently skip a shader. No GPU rendering yet.

Stage 2: adapt Vulkan backend resource/target lifetime to checked guest addresses, constants and portable vertex formats. Standalone offscreen GPU fixture before integrating with JNI/window. Explicit surface lifecycle, readback and backbuffer rotation; keep GLES fallback. Stage 3: permit complete validated D3D draws rather than callsite-specific reduced replay; first confirm RP6 menu and phase. Windows guarded-call continuation patch is specific to recompiler analysis and needs a real XBE rebuild plus resume runtime support; do not apply blindly. Windows canyon mip patch is for texture replacement, not the current first-phase blocker. Existing mathematical and IO fixes remain. CI and physical testing pending.

## Offscreen GPU milestone

The be538dc reference and shader compiler tests passed on CI. New fixture uses the same current Hulk HLSL-generated SPIR-V, reference ImageMemory and FrameFences, Vulkan 1.1 offscreen color attachment, real vertex buffers with observed 24-byte layouts, stage-0 sampled image/sampler, alpha blending, and host-coherent readback. Explicit varying locations prevent separately compiled VS/PS interface mismatches. Pixel proof covers untextured diffuse, half-alpha red over blue, and packed directional lighting modulated by an uploaded constant-color texture. CI uses Mesa lavapipe (software Vulkan implementation) deliberately, so this validates the API/translation, not RP6 GPU performance, guest texture upload, actual material values or gameplay. Still no APK Vulkan backend selection; future integration must use checked guest resources and support full D3D state. CI pending.

## Android hardware probe

ba397e5 passed all 3 Vulkan tests, including real offscreen pixels on llvmpipe: diffuse 0,255,0,255; blend 128,0,127,191; packed lit 0,0,64,255. Parallel portable recompilation failed exit 74 after synthetic IA-32/AArch64 execution passed and before the next fixture generation. The likely cause is llvm-readelf writing to a prematurely closed pipe (grep -q under pipefail); preserve the architecture check and drain its output rather than suppressing command failure. New Vulkan CI builds the identical pixel test as Android ARM64 PIE with API 29 and packages its SPIR-V, test script and license. Also add actual 16-bit index buffer draws and a two-stage texture combiner pixel proof; no synthetic shader substitution. The executable is a standalone diagnostic, not an APK or game backend. On-device RP6 result still required. Existing GLES branch remains untouched.

## RP6 hardware milestone and raster state integration

The user ran the 5136113 Android probe on Adreno 740: all five pixel comparisons passed (diffuse, outside triangle, blend, packed lighting, two-stage indexed modulation). This verifies the synthetic offscreen Vulkan path on physical hardware, not gameplay. Four workflows also passed for 5136113.

Next implementation replaces hardcoded blend and color settings with a reusable translation of the actual gpu::State NV2A render-state encodings, adds depth/stencil attachments with supported format selection, and exercises occlusion, disabling depth writes, stencil increment/rejection and write masks through real draws. Invalid W-buffer mode is rejected. These tests still use synthetic vertices and an offscreen target; guest-resource upload, dynamic shaders, swap/present, APK selection and first-phase gameplay are not yet integrated. GLES is untouched. New CI pending.

## Checked guest resource path

91d6e20 passed all four workflows. This change links the current Xbox resource converters into the Vulkan host and Android probes, stages decoded Xbox BGRA/DXT1 through a real buffer-to-image copy, consumes checked guest vertex/index memory for an indexed draw, and writes RGBA framebuffer results into a checked Xbox linear BGRA surface. Tests reject uncommitted index and texture spans. Synthetic Xbox resources exercise real runtime converters; no commercial game assets are included. The GPU fixture remains limited to a 2x2 texture and one 24-byte vertex layout. Not yet a full gpu::Backend, no APK Vulkan selection or gameplay claim. CI pending.

## Generalized GPU resource submission

dc54590 passed all four workflows. This update replaces the fixed 24-byte/three-vertex submission restriction with checked Vulkan binding and attribute plans, multiple stream offsets, variable-size staging buffers and native 16/32-bit index submission (including assembled quads). Four independently bound textures can be replaced and resized, with the actual dimensions supplied to the shader. New pixel fixtures cover resized DXT1, a rebuilt indexed quad and distinct texture stages. The current precompiled shaders still restrict accepted declarations and the offscreen target remains 64x64; general shader selection, runtime constants, persistent framebuffer and APK integration remain pending. No changes to GLES. CI pending.

## Runtime shader compilation milestone

de869ff passed all four workflows. The Vulkan probe now links glslang on host and Android ARM64, compiles fixed-function HLSL from the actual State and Assembled register/decode layout, and caches exact source/stage pairs with a bounded entry count. The pixel regression includes a new compact vertex declaration, repeat cache hits, alpha discard and alpha-reference changes without recompilation. Existing precompiled fixtures remain as reference checks. Programmable shader constants, full transforms/lighting, real texture state binding and window/APK integration are still incomplete; unsupported dynamic lighting and programs are explicitly rejected. New CI pending; the physical Adreno report is for the earlier build only.

## Persistent render target and guest transforms

At implementation time 4f26c57 passed Linux Vulkan draws, SPIR-V validation and both Android smoke/APK workflows; its Android shader cross-build and portable recompilation were still running. This update adds compatible LOAD and CLEAR render passes to preserve the color/depth/stencil attachments across separate synchronous submissions, and derives fixed-function world/view/projection and texture matrices from guest state. Explicit HLSL row-major matrix layout matches the Xbox row-vector representation. New pixel tests cover depth retention, translated geometry, preserved color and explicit frame reset. The new commit's CI is pending. Lighting/program constants, full resource lifetime and APK/runtime selection remain open; the offscreen renderer is not yet gameplay. GLES remains unchanged.

## D3D8 backend ABI integration

4f26c57 completed all four workflows. The 8aa78a6 CI was still running at implementation time. Extracted the synchronous Vulkan core and introduced VulkanBridgeBackend implementing the current gpu::Backend ABI. The existing XbeD3d8Bridge now has a Vulkan regression that dispatches Xbox Clear/Draw/Present and verifies guest framebuffer pixels. Target size is configurable; explicit clear and guest writeback are supported. Unsupported rotation, surface copy, depth-target binding, viewport variation, gamma and programmable state remain strict errors. This is a direct bridge integration in the standalone native probe, not yet selected in the APK or validated against a real game session. CI pending; GLES untouched.

## D3D8 ABI build correction

The d14f5b0 Vulkan workflow stopped at compile time because cpu/x86_context.h is not vendored. The other Android smoke and APK jobs passed. This correction uses the same pinned TGP482 XboxXBERecomp header and Git blob checksum already used by the GLES/APK workflows, passes its include directory to both host and Android CMake builds, and checks the fixture stack and return ABI. No renderer behavior is changed. CI pending.
