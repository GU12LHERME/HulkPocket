# HulkPocket: XboxXBE-Recomp Android feasibility (experimental branch)

Status: **not playable**. This branch preserves the current Thunderclap-based Android
diagnostic and evaluates a separate route using
[Hulk Recompiled](https://github.com/lookingcoding/hulk-ultimate-destruction-recomp-xbox-ver-)
and [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp).
No upstream game code, executable, ISO or generated source is copied here.

## Verified upstream implementation facts

- Hulk Recompiled is an **experimental playable Windows x64 build**, with
  documented movie, title screen, controls and junkyard-tutorial gameplay.
  The upstream STATUS.md explicitly does **not** claim a verified full campaign.
- The upstream CMakeLists.txt invokes `add_xbe_image` from the XboxXBE-Recomp
  SDK and compiles the game's image from `assets/default.xbe`.
  `build.bat` delegates to a PowerShell Windows build pipeline.
- The original Xbox image identifier in `config/game.toml` is
  `title = 0x56550039`; the SDK target is XDK 5849.
  Equal title IDs **do not** establish matching executable revisions.
- The Windows frontend includes an executable loader (linked against
  `shell32`) and a DLL output, `HulkRecompiled.dll`.
- `src/game/fibers.cpp` uses Win32 `CreateFiberEx`,
  `ConvertThreadToFiberEx`, `SwitchToFiber`, `DeleteFiber`,
  and `GetCurrentFiber`; simply using an Android compiler would not
  preserve the game's cooperative thread continuation semantics.
- Upstream's D3D11/D3D12 renderer depends on Direct3D and Windows
  graphics APIs. SDL3 gamepad availability is used by input,
  but does **not** imply the engine has an Android SDL video backend.

These observations come from reading upstream `README.md`, `STATUS.md`,
`CMakeLists.txt`, `config/game.toml`, `src/game/fibers.cpp` and
`src/common/game_info.cpp`.

## Critical-path Android gates

1. **Recompiler portability**: establish an x86-to-ARM64 generated
   source compilation path that produces an AArch64 ELF shared object,
   without requiring a Windows DLL host. The SDK currently targets the
   Windows-native app; this is not solved by adding an Android Gradle task.
2. **Host runtime**: replace or port Win32 filesystem, threading, timing,
   exception/SEH, fiber-stack switching, executable and dynamic
   symbol-loading behavior. Specifically test fiber suspend/resume of a
   nested C++ continuation rather than stubbing out the calls.
3. **Graphics**: implement a tested Android Vulkan or OpenGL ES backend
   translating the Xbox D3D8-style render operations, shaders and
   texture/palette paths. Do not claim rendering success from a
   blank window.
4. **Audio, controller, disc and save I/O**: adapt host APIs to Android;
   verify the original disc's file layout and proper storage permissions,
   gamepad buttons/axes and real audio playback on RP6.
5. **Local game-code generation**: verify the exact local `default.xbe`
   revision, generate recompiled C++ without uploading copyrighted
   output to public Actions, then compile and link a real motor image.
6. **Device acceptance**: only call the port *playable* after RP6 evidence
   of title screen, rendered tutorial, controls, audio, saving and
   sustained gameplay without a runtime crash.

## APK delivery

The public repository currently builds **HulkPocket 0.2.0 diagnostic APK**
through `.github/workflows/android-apk.yml`. It verifies the ISO and
loads a tiny JNI ARM64 bridge, but it explicitly reports that the native
game motor is **not integrated**. This artifact must not be labeled
`Hulk Ultimate Destruction Android playable`.

An actual playable APK cannot currently be produced from the public
sources alone. CI cannot substitute for the missing ARM64 runtime and
renderer nor privately generated game-derived inputs.

Keep this work on `experiment-xbe-recomp-android` until a real
AArch64 engine can compile, link and boot on the RP6. Avoid disrupting
the currently working diagnostic APK on `main`.

## First practical ARM64 code integration smoke

`.github/workflows/xbe-recomp-arm64.yml` clones the upstream Windows port
into a temporary GitHub runner directory (nothing is copied into HulkPocket),
runs the original `tests/simulation.cpp` timing regression on the Linux host,
then cross-compiles the **actual upstream** `src/common/rcf.cpp` archive
reader and `src/common/simulation.h` timing logic as an Android ARM64
shared library. It links with `-Wl,-z,defs` and checks AArch64/ELF64
and the exported entry point.

This is a meaningful portability checkpoint for two game-related
components, **not** the recompilation of Xbox machine code, the game
rendering engine or an Android gameplay APK. Passing this test would
show the selected logic ports to the Android NDK without Windows APIs.
At publication, the new workflow has not yet been independently
confirmed as successful.

## Next real-game portability probe

The Android ARM64 workflow now also includes upstream
`src/common/actor_owner_guard.h`, the original-Xbox instruction
decoder used by the playable Windows port's Lightning Strike crash fix.
A small test checks the actual owner-field read instruction sequence,
guest registers, and crash-guard decision. The same probe is executed
on the Linux host using `ctypes` and separately compiled into the
Android ARM64 `.so`.

This tests portable game logic and crash-handling helpers only;
**native Xbox execution, graphics and gameplay are not implemented**.
The new commit must pass GitHub Actions before it is called validated.

## Disc version gate: EU vs USA

The playable Windows Hulk Recompiled port uses **different generated
code and hook addresses** for the EU and USA XBE revisions:
`config/game.toml` starts at `0x0020AAC6` (EU), while the
USA game uses `0x0020A016` (see upstream `addresses.h`).
Both carry Title ID `56550039`, so using only the Title ID risks
building the wrong game code.

In this experimental branch, `tools/xbe_info.py` now decodes the
original Xbox retail/debug entry-point XOR field at XBE offset
`0x128`, validates that candidates are inside the declared image
address range, and reports `hulk_recompiled_candidate` as
`eu`, `usa`, or `null`. It checks the Title ID too.
Synthetic-file regression tests run in the Android ARM64 workflow.
This detection is *not proof of exact executable compatibility*: exact
section hashes and an actual device boot still have to be checked.
It only inspects a local XBE and does not upload the game.

## AArch64 native fiber-switch prototype

A prototype `native/aarch64_context.S` saves and restores AAPCS64
callee-saved registers (x19–x30, SP and d8–d15) between **two separate
native stacks**. Its header defines a fixed context layout and a
standalone test constructs a fresh fiber, switches into it, suspends
back to the caller and resumes the same native C++ continuation.
The Android ARM64 workflow now cross-compiles the prototype into an
Android ELF shared object and runs the corresponding Linux AArch64 test
under QEMU. This test is pending GitHub Actions verification.

This is foundational work for replacing the Windows-specific
`CreateFiberEx`/`SwitchToFiber` code. It does **not** implement Xbox
fiber record mapping, exception/SEH handling, TLS, stack ownership,
fiber deletion, cross-thread migration or Android runtime integration.
Do not link the prototype to the game before its stack lifecycle,
ownership and guest-record semantics are implemented and tested.

## Managed Android ARM64 fiber primitive (experimental)

`native/native_fiber.h` and `native/native_fiber.cpp` provide a
single-thread, stack-owning fiber wrapper over the AArch64 switch
assembly. The implementation allocates stacks with `mmap`, applies
inaccessible guard pages at each end, and offers `Resume`, `Yield`,
completion detection and a strict creator-thread rule. Normal function
return from the fiber entry transfers back to the caller; returning to
the arbitrary stack bootstrap is forbidden.

`native/test_native_fiber.cpp` checks the lifecycle:
resume → yield → resume → yield → resume → finish, then refuses a
restart and rejects a yield without an active fiber. CI cross-compiles
the wrapper for Android ARM64, and compiles and runs a corresponding
AArch64 Linux binary under QEMU. This new test is **pending GitHub
Actions confirmation**.

The wrapper is **not yet connected to the game**: mapping the game's
Xbox fiber records and TLS, native exceptions, saved guest registers,
stack lifetime and switching semantics will require further work and
real-game tests. It is also not a graphics backend or an APK.

## Guest-to-guest Xbox fiber scheduling (prototype)

The new `native/xbox_fiber_scheduler.{h,cpp}` models a central
dispatcher using distinct guest IDs and separately owned ARM64 stacks.
A guest calls `SwitchTo(id)`, which suspends the current C++ stack and
returns control to the host dispatcher. The dispatcher resumes the
requested guest stack; the caller's `SwitchTo` returns **only after**
another guest switches back. This approximates an essential semantic
difference between the original Xbox/Windows fiber API and a trivial
callback loop.

`test_xbox_fiber_scheduler.cpp` exercises an interleaved
A → B → A → B → A sequence, then resumes the suspended B stack;
it also rejects duplicate IDs and starting a completed fiber.
GitHub Actions cross-compiles the dispatcher as a real Android ARM64
shared library and executes its Linux ARM64 test under QEMU.

**Validated:** GitHub Actions run
[45713686889](https://github.com/GU12LHERME/HulkPocket/actions/runs/45713686889)
completed successfully, including the AArch64 QEMU guest-fiber dispatcher
test and Android ARM64 shared-object link. This is a host-side foundation, not an
Xbox XBE execution engine. It does not yet manipulate the original
guest TLS, XBE fiber records, Xbox register save/restore, or
XboxXBE-Recomp's Win32 fiber hooks. Graphics, kernel services,
and game bootstrap remain unported. Do not claim playable APK output
from successful dispatcher tests.

## XDK 5849 guest fiber record adapter

`native/xbox_guest_fiber_state.h` reproduces the *guest-side* x86
`SwitchToFiber` frame mutation documented by the playable upstream
Windows implementation: it pushes the guest's EBP/ESI/EDI/EBX and
`fs:[0]` exception-chain head, stores the guest ESP at fiber-record
offset `+0x0C`, updates guest TLS at `+0x10`, and restores the
destination guest's SEH and registers before consuming its entry/return
address. All accesses go through injected guest-memory read/write
callbacks, **not** raw pointers into the Android process. Tests use
synthetic guest memory and verify the return trip A→B→A. CI compiles
the test for Android ARM64 and executes an ARM64 Linux build under QEMU.

This adapter is still deliberately **separate** from the native
AArch64 stack dispatcher. Integrating them requires connecting
XboxXBE-Recomp's X86Context, its real guest TLS and memory mapping,
and the generated Xbox code. Do not claim game boot or gameplay based
on these standalone test results. Latest test is pending CI.

## First combined guest/host fiber switch test

`native/xbox_fiber_bridge.h` combines **both previously separate**
pieces of runtime functionality: `SaveRestoreXboxFiber` applies the
original XDK guest-x86 stack, SEH and TLS record mutation, while
`XboxFiberScheduler::SwitchTo` suspends and resumes the host's
AArch64 C++ stack. Before changing guest state, the bridge checks
that guest TLS names the current native fiber and that the target
is present and not completed.

`native/test_xbox_fiber_bridge.cpp` tests a complete A→B→A→B
sequence using real ARM64 stack switches and synthetic checked Xbox
memory. Both the first destination restore and the resumed source
registers/SEH/guest SP are checked inside their separate native C++
continuations. The workflow cross-compiles the test with the Android
NDK and runs the Linux AArch64 version under QEMU.

**Important:** this is a host/guest integration *prototype* with
synthetic game memory; it is still not wired into the XboxXBE-Recomp
SDK, its `X86Context`, real XBE executable, or graphics engine.
The workflow triggered by the bridge commit must pass before claiming
successful verification.

## Actual XboxXBERecomp SDK X86Context integration gate

`native/xbe_sdk_fiber_adapter.h` now includes the **real**
`cpu/x86_context.h` from
[TGP482/XboxXBERecomp](https://github.com/TGP482/XboxXBERecomp)
(the upstream GitHub repository's current canonical name). The adapter
reads the SDK's x86 register fields and guest `fs_base`, applies
the XDK 5849 guest memory save/restore, writes the resulting state
back into the same `X86Context`, then switches to the corresponding
native AArch64 stack. Crucially it does **not** overwrite the shared
context with a stale copy when a suspended C++ continuation resumes.

A regression test runs two host fiber continuations A→B→A→B against
a synthetic checked guest address space while asserting actual SDK
register, TLS and SEH values. The workflow clones the public SDK,
compiles this exact integration source with the Android ARM64 NDK,
and executes the Linux AArch64 integration test under QEMU.

The **next production blocker** is wiring this adapter to the
actual `X86_FUNC(HULK_SUB_SWITCH_TO_FIBER)` hook and implementing the
host runtime with Android-compatible services. In particular, the
upstream SDK CMake script explicitly links Windows PE/DLL import
libraries and `d3d12`, `xaudio2`, `dxgi` and Win32 GUI backends;
its unmodified CMake project cannot build a playable Android ELF.
A successful integration test is not game initialization or a
rendered frame. This checkpoint is awaiting its GitHub Actions run.

## Hulk-specific `SwitchToFiber` call-chain adapter

`native/hulk_xdk_fiber_hook.h` now resolves the exact XDK 5849
indirection chain used by the playable Windows Hulk port:
`next = guest[ctx.esp+4]`, `stack_base = guest[ctx.fs_base+4]`,
`tls_index = guest[region.fiber_tls_index_va]`,
`tls = guest[stack_base+4*tls_index]`,
`current = guest[tls+0x10]`. It checks invalid/null addresses
and disagreement between the native scheduler and the real SDK
`X86Context`, then delegates to the existing ARM64 guest/host
fiber bridge. Region-specific EU/USA TLS, switch and bootstrap
addresses come from upstream `src/common/addresses.h`.

`native/test_hulk_xdk_fiber_hook.cpp` uses an actual SDK
`X86Context` and **synthetic** Xbox TLS/stack memory to test
two suspended native ARM64 continuations. CI runs the host
AArch64 build under QEMU and cross-compiles the test translation
unit with the Android NDK.

**Known missing functionality:** When the target guest fiber does
not exist in the native dispatcher, the adapter validates the
guest's expected bootstrap address but deliberately rejects
creation. A real `x86_icall` trampoline, root-fiber registration,
guest TLS/exception safety and lifetime management must be
implemented before this can replace the live Hulk
`X86_FUNC(HULK_SUB_SWITCH_TO_FIBER)` hook. No actual XBE code
is executed in this test, and this does not implement graphics,
audio or an Android gameplay APK. The new workflow must pass
before this checkpoint is considered validated.

## CI upstream API drift (2026-11-25)

Run [45715461264](https://github.com/GU12LHERME/HulkPocket/actions/runs/45715461264)
passed all guest/host fiber, native AArch64, SDK X86Context and
Hulk-specific TLS-chain tests, but failed in the final upstream
game logic probe because upstream Hulk Recompiled at
`6603e8a4021a06331f63310ce47b42315d9dbfc6` no longer includes
`src/common/actor_owner_guard.h`. The workflow now tests supported
simulation clock, RCF alignment and case-insensitive path handling
without depending on the removed header. This is a **test compatibility
fix**, not progress on the still unimplemented Android graphics or
guest-code launch trampoline. Follow-up workflow
[45715778455](https://github.com/GU12LHERME/HulkPocket/actions/runs/45715778455)
must pass before the fix is considered validated.

## Lazy XDK fiber bootstrap: first guest code invocation boundary

`native/xbe_guest_fiber_bootstrap.h` owns a native ARM64 stack
for each newly discovered XDK fiber record and stores its stable guest
entry address. After the Hulk-specific XDK TLS chain and expected
region's bootstrap address have been validated,
`HulkXdkSwitchToFiber` can register a new fiber through a
`XbeGuestFiberBootstrap` instance; the native-stack entry calls the
provided `GuestCall(X86Context&, uint8_t*, uint32_t)`. The intended
production binding is XboxXBERecomp's real `x86_icall`, on the
original guest context and memory base.

`native/test_hulk_xdk_fiber_bootstrap.cpp` tests registration while
an existing ARM64 guest fiber is running, A → new B → A → B native
stack switching, and checks that B's initial entry receives the
region-correct XDK bootstrap address and shared SDK context. The
test uses a **mock guest-call callback and synthetic guest memory**,
not the game's XBE, and it flags unexpected bootstrap return. If
the guest-code entry returns, production must trap/terminate the
thread: there is no safe implicit continuation to a previously
suspended fiber. CI cross-compiles the test using the Android NDK
and runs the Linux ARM64 binary under QEMU.

**Still missing:** the actual game hook registration and root-fiber
conversion on its current kernel thread, callback binding to the
recompiled game's `x86_icall`, kernel boot and Xbox resource
services, graphics and audio rendering. The test added in
commit `c00cfc8` is not yet verified on GitHub Actions.

## x86_icall symbol boundary (test double, not game code)

The upstream SDK's `runtime/cpu/dispatch.cpp` implements:
`void x86_icall(X86Context&, uint8_t*, uint32_t)`. That file
is Windows-only: it loads the recompiled PE DLL through
`LoadLibraryW`/`GetProcAddress`, uses other Win32 services,
then dispatches through a generated Xbox function mapping.

`native/xbe_sdk_icall_binding.h` now binds the new-fiber trampoline
to **this exact C++ function symbol/signature**. The matching
`native/test_xbe_sdk_icall_binding.cpp` provides a **test double**
for `x86_icall` with that signature and verifies that a lazily
created native ARM64 fiber invokes it, that its guest continuation
switches to another ARM64 stack, and that the resumed function sees
the expected Xbox state. Linux ARM64 testing runs on QEMU, and
Android NDK checks compile the integration test.

**This does not execute the SDK's real `dispatch.cpp`** or any
copyrighted Xbox binary. The actual Android dispatch implementation
must replace the Windows DLL loading/function-mapping code, and a
real game hook must supply guest memory, kernel-thread ownership,
and root-fiber handling. CI verification is pending.

## Portable ARM64 Xbox function dispatcher (actual implementation)

`native/portable_xbe_dispatch.h/.cpp` implements an Android-compatible
replacement for **the function-map lookup and call subset** of
XboxXBERecomp `runtime/cpu/dispatch.cpp`:

- Uses the SDK's real `X86FuncMapping` type with guest function VA
  and `X86Func*` host function pointer.
- Compiles a sorted guest-address mapping with duplicate, null and
  out-of-bounds validation; unmapped guest calls fail clearly.
- Implements `x86_icall(X86Context&, uint8_t*, uint32_t)` with the
  exact C++ signature called by the SDK and the new fiber bootstrap.
- Uses thread-local scoped dispatch ownership to keep guest kernel
  threads distinct and keeps suspended native fiber continuations
  bound to the correct owning host thread.
- Links the dispatcher, native ARM64 fiber scheduler, guarded stacks
  and AArch64 assembly into a single 16-KiB-page-compatible Android
  ELF shared library `libhulkpocket-portable-xbe-dispatch.so`.

`native/test_portable_xbe_dispatch.cpp` tests the **real portable
x86_icall implementation**, not a mock `x86_icall`, against
SDK-format test mappings and a lazily created ARM64 fiber that calls
a test guest function. It checks register/TLS restoration, unmapped
entry rejection, duplicate address validation and teardown of the
scoped runtime binding. The new GitHub Actions build publishes the
experimental `.so` only after all tests succeed.

**This library is not yet a game port.** Mappings currently come from
test functions, not Hulk's recompilation. The Windows recompiler
outputs a PE/DLL-oriented code module; a production Android
pipeline must generate/link ELF ARM64 guest code, provide the full
guest-memory/kernel services, and implement the existing Direct3D
and XAudio2 frontends in Android-supported backends. The `.so`
is solely a native runtime component for further porting; it is
not an APK and cannot display or execute Hulk on its own.

## Xbox kernel import ordinal dispatch

The portable dispatcher now recognizes the XboxXBERecomp SDK
`0xFE000000 + ordinal * 4` kernel thunk range (400 ordinals)
and routes valid entries through an optional native
`KernelCall(X86Context&, uint8_t*, uint32_t)` callback. It
records the last guest target in `ctx.last_icall` like the
upstream dispatcher, and rejects unaligned or unavailable
thunks without falling through into unrelated game code.
The ARM64 regression exercises ordinal 17 and failure cases.

This is an **invocation boundary**, not a port of all 400
Xbox kernel services. Kernel memory, threads, filesystem,
audio and GPU services remain to be implemented or replaced.

## Android ELF loading as a replacement for Win32 LoadLibraryW

`native/portable_xbe_image.h/.cpp` now implements a native
`dlopen(RTLD_NOW | RTLD_LOCAL)`/ `dlsym` image loader for
AArch64/Linux or Android ELF guest-code modules. It validates
the existing SDK's exported `X86CodeBase`, `X86CodeEnd`,
`X86EntryPoint`, and null-terminated `X86FuncMappings`
and builds a `PortableXbeDispatch` from that mapping.

`native/test_portable_xbe_image_module.cpp` exports these real
SDK symbol names from a **synthetic host function module**,
and `native/test_portable_xbe_image.cpp` tests opening the
shared library, inspecting the guest entry, finding functions,
dispatching them by Xbox virtual address, and rejecting unmapped
functions. CI runs the dynamic loader test on Linux and
cross-compiles both loader and synthetic module to Android ARM64.

This is the real operating-system loading mechanism, but
**not a transpiler**: a Windows PE DLL or original Xbox x86
XBE cannot be loaded via `dlopen`, and the generated game
functions must be recompiled specifically as Android AArch64
ELF first. The XBE must remain with the user, not committed
to the public repository.

**Security:** Loading any ELF can execute its initialization
code before export validation. Only load trusted modules
generated from personally owned original game files, never
arbitrary third-party game-code libraries.

## Safe ARM64 guest fiber fault propagation

`NativeFiber::Bootstrap` used to call `abort()` if guest-side
C++ code threw an exception. It now stores `std::exception_ptr`,
finishes the guest fiber, switches back to its saved host ARM64
stack, and rethrows from `NativeFiber::Resume` **only after**
the original host stack/context and TLS active fiber pointer
have been restored. `XboxFiberScheduler::Run` already catches
host stack exceptions to reset its dispatch state.

`native/test_native_fiber.cpp` tests both normal suspend/resume
and a guest callback which yields once then throws an error. It
asserts the error reaches the caller and the failed fiber cannot
resume. This prevents ordinary unsupported guest operations from
turning directly into process aborts during early game startup.
This is not a substitute for Windows SEH/x86 exception
semantics in game code; those remain separate work.

## Latest verified CI checkpoint — 2026-11-25

[GitHub Actions run 45717478128](https://github.com/GU12LHERME/HulkPocket/actions/runs/45717478128)
**passed**, including the actual portable `x86_icall` implementation,
Xbox kernel ordinal bridge, native Android ELF `dlopen` loader,
synthetic ELF SDK exports, guest fiber bootstrap and safe exception
propagation under QEMU ARM64. The Android NDK produced a linked
AArch64 16-KiB-compatible `.so`, published as the
[experimental runtime artifact](https://github.com/GU12LHERME/HulkPocket/actions/runs/45717478128/artifacts/15069738727).
The artifact contains **only the infrastructure runtime**; no
Xbox game assets or recompilation of the Hulk XBE are included.

## Android-generated guest code preparation checkpoint

An inspection of the actual SDK `tools/recomp/codegen_emit.cpp` showed that
the recompiler already emits portable **C++ instruction bodies**, but the
generated mapping table and support macros target Windows PE/DLL linkage.
The `tools/prepare_xbe_elf.py` script accepts a directory of SDK-generated
C++ and the upstream SDK source, then:

- Copies generated instruction partitions unchanged, preserving recompiled code.
- Rewrites the generated mapping table definitions to export the expected
  `X86CodeBase`, `X86CodeEnd`, `X86EntryPoint`,
  `X86FuncMappings`, and related names through ELF C linkage.
- Copies the SDK runtime's x86 context/header, replacing only Windows
  import/export/weak-alias macros with ELF-compatible definitions.
- Rejects unknown upstream mapping or macro layouts instead of silently
  generating a broken Android build.

The GitHub Actions job tests a synthetic SDK-style generated directory
with the **real upstream SDK runtime header**, compiles it into
Linux and Android ARM64 shared libraries, and checks the exported symbols
and the Linux `dlopen`→`x86_icall` dispatch.

**Not validated yet with any real Hulk XBE.** Because `xbe_recomp` needs
an XBE from a lawful game copy and no XBE should be uploaded to the
public GitHub repository, this integration uses synthetic generated
C++. Actual instruction translation, hooking, exception behavior and
guest kernel requirements still need extensive validation. The
updated CI run should be checked before considering this passed.

## Verified SDK-generated-style ELF module build (2026-11-25)

[GitHub Actions run 45718168101](https://github.com/GU12LHERME/HulkPocket/actions/runs/45718168101)
**passed**, including the new `Prepare SDK-generated Xbox C++ for
Android ELF and verify exports` CI step. It created an asset-free
XboxXBERecomp-style C++ mapping and instruction partition, used
the upstream SDK's actual `runtime/cpu/x86_ops.h`, applied
`tools/prepare_xbe_elf.py`, linked the resulting module as
Linux ELF for a real `dlopen` / native function-call check,
and compiled the same prepared sources into an Android ARM64
ELF library with the expected exported `X86FuncMappings`.

The generated C++ and code addresses in this test are synthetic.
Actual Hulk XBE recompiled instructions, asset loading and
gameplay remain entirely untested. This is a source compatibility
checkpoint rather than a playable build.
