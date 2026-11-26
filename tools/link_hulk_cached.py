#!/usr/bin/env python3
"""Private diagnostic ELF link, not a playable game build."""
import argparse
import pathlib
import subprocess
import tempfile
import hashlib
from prepare_xbe_elf import prepare
from build_xbe_android import generated_sources
from supplemental_guest_mapping import generate
from discover_hulk_viewport_writer import generate_writer
from discover_hulk_block_on_time import generate_block_on_time
from discover_hulk_startup_text import generate_startup_text
from discover_hulk_fibers import generate_fibers

def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("sdk", "generated", "cache", "ndk-bin", "output"):
        p.add_argument("--"+name, required=True, type=pathlib.Path)
    a = p.parse_args()
    root = pathlib.Path(__file__).resolve().parent.parent
    compiler = a.ndk_bin / "clang++"
    target = subprocess.check_output([str(compiler), "-dumpmachine"], text=True).strip()
    if not target.startswith("aarch64") or "android" not in target:
        raise RuntimeError("Android AArch64 compiler required")
    with tempfile.TemporaryDirectory(prefix="hulk-link-") as tmp:
        work = pathlib.Path(tmp)
        prepared = work / "prepared"
        prepare(a.generated, a.sdk, prepared)
        fingerprint = hashlib.sha256()
        fingerprint.update(subprocess.check_output([str(compiler), "--version"]))
        fingerprint.update(str(compiler.resolve()).encode())
        for file in sorted(prepared.rglob("*")):
            if file.is_file():
                fingerprint.update(str(file.relative_to(prepared)).encode())
                fingerprint.update(hashlib.sha256(file.read_bytes()).digest())
        fingerprint.update((root / "tools/build_xbe_android.py").read_bytes())
        cache = a.cache / fingerprint.hexdigest()
        objects = [cache / (s.stem + ".o") for s in generated_sources(prepared)]
        if not all(o.is_file() for o in objects):
            raise RuntimeError("Matching complete cache not found; no recompilation attempted")
        print(f"Reusing {len(objects)} cached objects", flush=True)
        # Write outside the prepared input tree: the existing fingerprint
        # and every cached game object remain unchanged.
        count = generate(prepared, work / "hulk_extra_mapping.h")
        print(f"Supplemental verified guest function entries: {count}", flush=True)
        viewport_writer=generate_writer(prepared,work/"hulk_verified_viewport_writer.h")
        print(f"Verified local viewport writer instrumentation: {viewport_writer}",flush=True)
        block_on_time=generate_block_on_time(prepared,work/"hulk_verified_block_on_time.h")
        print(f"Verified local D3D_BlockOnTime adapter: {block_on_time}",flush=True)
        startup_text=generate_startup_text(prepared,work/"hulk_verified_startup_text.h")
        print(f"Verified local startup text observer: {startup_text}",flush=True)
        fibers=generate_fibers(prepared,work/"hulk_verified_fibers.h")
        print(f"Verified local XDK fiber replacement: {fibers}",flush=True)
        native_names=["portable_xbe_dispatch","xbe_diagnostic_runtime","xbe_first_entry_probe","xbe_d3d8_entry_guard"]
        if fibers:
            native_names+=["hulk_live_fiber_hooks","xbox_fiber_scheduler","native_fiber"]
            obj=work/"aarch64_context.o"
            subprocess.run([str(compiler),"-fPIC","-c",str(root/"native/aarch64_context.S"),"-o",str(obj)],check=True)
            objects.append(obj)
        for name in native_names:
            obj = work / (name + ".o")
            subprocess.run([str(compiler), "-std=c++20", "-O2", "-fPIC",
                            "-I", str(prepared), "-I", str(work), "-I", str(root / "native"),
                            "-c", str(root / "native" / (name + ".cpp")),
                            "-o", str(obj)], check=True)
            objects.append(obj)
        a.output.parent.mkdir(parents=True, exist_ok=True)
        pending = a.output.with_suffix(".partial.so")
        try:
            subprocess.run([str(compiler), "-shared", "-Wl,-z,defs",
                            "-Wl,-z,max-page-size=16384", *map(str, objects),
                            "-lm", "-o", str(pending)], check=True)
            header = subprocess.check_output([str(a.ndk_bin / "llvm-readelf"), "-h", str(pending)], text=True)
            if "AArch64" not in header or "ELF64" not in header:
                raise RuntimeError("Unexpected ELF architecture")
            symbols = subprocess.check_output([str(a.ndk_bin / "llvm-nm"), "-D", "--defined-only", str(pending)], text=True)
            names = {line.split()[-1] for line in symbols.splitlines() if line.split()}
            for name in ("X86FuncMappings", "X86CodeBase", "X86CodeEnd", "X86EntryPoint", "hulk_entry_probe", "hulk_entry_execute", "hulk_entry_execute_with_kernel"):
                if name not in names:
                    raise RuntimeError("Missing export: " + name)
            if viewport_writer and not any(len(fields:=line.split())>=3 and fields[-2:]==["T","sub_0037F720"] for line in symbols.splitlines()):
                raise RuntimeError("Verified viewport writer override not linked")
            if block_on_time and not any(len(fields:=line.split())>=3 and fields[-2:]==["T","sub_0038A2C0"] for line in symbols.splitlines()):
                raise RuntimeError("Verified D3D_BlockOnTime override not linked")
            if startup_text and not any(len(fields:=line.split())>=3 and fields[-2:]==["T","sub_0029B330"] for line in symbols.splitlines()):
                raise RuntimeError("Verified startup text observer not linked")
            if fibers:
                for name in ("sub_0035551D","sub_0035550A"):
                    if not any(len(fields:=line.split())>=3 and fields[-2:]==["T",name] for line in symbols.splitlines()):
                        raise RuntimeError("Verified fiber override not linked: "+name)
                    print("Verified guest fiber override:",name,flush=True)
            # Verify strong override, not just a symbol name that could be weak.
            import re
            header_text = (prepared / "image_symbols.h").read_text()
            named = bool(re.search(r"^#define IMAGE_NAMED 1$", header_text, re.M))
            for hook in ("Direct3D_CreateDevice", "D3DDevice_DrawVertices",
                         "D3DDevice_DrawIndexedVertices", "D3DDevice_Swap", "D3DDevice_Clear",
                         "D3DDevice_BlockUntilVerticalBlank", "XLaunchNewImageA",
                         "XInitDevices", "XInputOpen", "XInputClose", "XInputGetState",
                         "XInputSetState", "XInputGetCapabilities",
                         "crt_sin", "crt_cos", "crt_tan", "crt_asin", "crt_acos",
                         "crt_atan", "crt_sqrt", "crt_log", "crt_log10", "crt_pow",
                         "_CIacos", "_CIasin",
                         "D3DDevice_SetMaterial", "D3DDevice_SetLight", "D3DDevice_LightEnable",
                         "D3DDevice_SetRenderState_Simple",
                         "D3DDevice_SetRenderStateNotInline",
                         "D3DDevice_SetTextureStageStateNotInline",
                         "D3DDevice_MakeSpace",
                         "D3D_MakeRequestedSpace",
                         "D3DDevice_SetRenderState_PSTextureModes",
                         "D3DDevice_SetRenderState_VertexBlend",
                         "D3DDevice_SetRenderState_FogColor",
                         "D3DDevice_SetRenderState_FillMode",
                         "D3DDevice_SetRenderState_BackFillMode",
                         "D3DDevice_SetRenderState_TwoSidedLighting",
                         "D3DDevice_SetRenderState_NormalizeNormals",
                         "D3DDevice_SetRenderState_ZEnable",
                         "D3DDevice_SetRenderState_StencilEnable",
                         "D3DDevice_SetRenderState_StencilFail",
                         "D3DDevice_SetRenderState_FrontFace",
                         "D3DDevice_SetRenderState_CullMode",
                         "D3DDevice_SetRenderState_TextureFactor",
                         "D3DDevice_SetRenderState_LogicOp",
                         "D3DDevice_SetRenderState_EdgeAntiAlias",
                         "D3DDevice_SetRenderState_MultiSampleAntiAlias",
                         "D3DDevice_SetRenderState_MultiSampleMask",
                         "D3DDevice_SetRenderState_MultiSampleMode",
                         "D3DDevice_SetRenderState_MultiSampleRenderTargetMode",
                         "D3DDevice_SetRenderState_ShadowFunc",
                         "D3DDevice_SetRenderState_LineWidth",
                         "D3DDevice_SetRenderState_SampleAlpha",
                         "D3DDevice_SetRenderState_Dxt1NoiseEnable",
                         "D3DDevice_SetRenderState_YuvEnable",
                         "D3DDevice_SetRenderState_OcclusionCullEnable",
                         "D3DDevice_SetRenderState_StencilCullEnable",
                         "D3DDevice_SetRenderState_RopZCmpAlwaysRead",
                         "D3DDevice_SetRenderState_RopZRead",
                         "D3DDevice_SetRenderState_DoNotCullUncompressed",
                         "DirectSoundCreate",
                         "IDirectSound_Release",
                         "IDirectSound_GetSpeakerConfig",
                         "DirectSoundDoWork",
                         "DirectSoundCreateBuffer",
                         "IDirectSound_CreateSoundBuffer",
                         "IDirectSoundBuffer_Release",
                         "IDirectSoundBuffer_SetBufferData",
                         "IDirectSoundBuffer_Play",
                         "IDirectSoundBuffer_Stop",
                         "IDirectSoundBuffer_GetStatus",
                         "IDirectSoundBuffer_SetVolume",
                         "IDirectSoundBuffer_SetFrequency",
                         "IDirectSoundBuffer_SetCurrentPosition",
                         "IDirectSoundBuffer_GetCurrentPosition",
                         "CDirectSoundStream_Discontinuity",
                         "CDirectSoundStream_Flush",
                         "CDirectSoundStream_FlushEx",
                         "CDirectSoundStream_GetInfo",
                         "CDirectSoundStream_GetStatus",
                         "CDirectSoundStream_Pause",
                         "CDirectSoundStream_Process",
                         "CDirectSoundStream_SetFilter",
                         "CDirectSoundStream_SetFormat",
                         "CDirectSoundStream_SetFrequency",
                         "CDirectSoundStream_SetHeadroom",
                         "CDirectSoundStream_SetI3DL2Source",
                         "CDirectSoundStream_SetMaxDistance",
                         "CDirectSoundStream_SetMinDistance",
                         "CDirectSoundStream_SetMixBins",
                         "CDirectSoundStream_SetMixBinVolumes",
                         "CDirectSoundStream_SetPosition",
                         "CDirectSoundStream_SetVelocity",
                         "CDirectSoundStream_SetVolume",
                         "DirectSoundCreateStream", "IDirectSound_CreateSoundStream",
                         "CDirectSoundStream_AddRef", "CDirectSoundStream_Release",
                         "DirectSoundUseFullHRTF",
                         "IDirectSound_CommitDeferredSettings",
                         "IDirectSound_CommitEffectData",
                         "IDirectSound_DownloadEffectsImage",
                         "IDirectSound_EnableHeadphones",
                         "IDirectSound_GetEffectData",
                         "IDirectSound_GetOutputLevels",
                         "IDirectSound_SetDistanceFactor",
                         "IDirectSound_SetDopplerFactor",
                         "IDirectSound_SetEffectData",
                         "IDirectSound_SetI3DL2Listener",
                         "IDirectSound_SetMixBinHeadroom",
                         "IDirectSound_SetOrientation",
                         "IDirectSound_SetPosition",
                         "IDirectSound_SetRolloffFactor",
                         "IDirectSound_SetVelocity",
                         "IDirectSound_SynchPlayback",
                         "IDirectSoundBuffer_Lock",
                         "IDirectSoundBuffer_Pause",
                         "IDirectSoundBuffer_SetAllParameters",
                         "IDirectSoundBuffer_SetEG",
                         "IDirectSoundBuffer_SetFilter",
                         "IDirectSoundBuffer_SetFormat",
                         "IDirectSoundBuffer_SetHeadroom",
                         "IDirectSoundBuffer_SetI3DL2Source",
                         "IDirectSoundBuffer_SetLoopRegion",
                         "IDirectSoundBuffer_SetMaxDistance",
                         "IDirectSoundBuffer_SetMinDistance",
                         "IDirectSoundBuffer_SetMixBins",
                         "IDirectSoundBuffer_SetMixBinVolumes",
                         "IDirectSoundBuffer_SetOutputBuffer",
                         "IDirectSoundBuffer_SetPlayRegion",
                         "IDirectSoundBuffer_SetPosition",
                         "IDirectSoundBuffer_SetRolloffCurve",
                         "IDirectSoundBuffer_SetVelocity",
                         "IDirectSoundBuffer_StopEx",
                         "IDirectSoundBuffer_Unlock",
                         "IDirectSoundStream_SetEG",
                         "XAudioDownloadEffectsImage"):
                if not re.search(r"^#define HAS_" + hook + r" 1$", header_text, re.M):
                    continue
                match = re.search(r"^#define SYM_" + hook + r" ([0-9A-Fa-f]{8})$", header_text, re.M)
                if not match:
                    raise RuntimeError("Missing generated symbol for " + hook)
                name = "sub_" + (hook if named else match.group(1))
                if not any(len(fields := line.split()) >= 3 and fields[-2:] == ["T", name]
                           for line in symbols.splitlines()):
                    raise RuntimeError("Strong guest hook not linked: " + name)
                print("Verified guest override:", hook, name, flush=True)
            pending.replace(a.output)
        finally:
            pending.unlink(missing_ok=True)
    print("Diagnostic ARM64 ELF linked:", a.output)
    print("Guest execution and unsupported runtime operations remain unvalidated.")

if __name__ == "__main__":
    main()
