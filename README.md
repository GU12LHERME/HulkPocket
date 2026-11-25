# HulkPocket

Experimental native ARM64 Android port project for The Incredible Hulk: Ultimate Destruction (original Xbox). Target device: Retroid Pocket 6.

Status: initial infrastructure only; no playable build.

Upstream reference: https://github.com/matthewfcarlson/thunderclap . No upstream code is redistributed here pending license verification. Users must supply their own legally obtained game data locally. Never commit ISO, XBE, extracted assets or generated game code.

## Milestones

1. Android NDK ARM64 smoke test and CI.
2. Verify upstream permissions and reproduce native build locally with owned game data.
3. Resolve ABI and unresolved-symbol issues; reject placeholder implementations on reachable game paths.
4. Package native engine with SDL3 and validate boot on device.
5. Validate gameplay, rendering, audio, saves, controls and performance before release.

## Português

Projeto experimental de port nativo ARM64 para Android, focado no Retroid Pocket 6. Ainda não executa o jogo. A primeira etapa valida a infraestrutura de compilação; os dados originais permanecem locais.
