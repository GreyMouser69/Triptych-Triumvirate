# Voidforged Client Integration Report - Triptych Triumvirate (NMS)

- **Date:** 2026-11-12
- **Destination repo:** Triptych-Triumvirate — branch `voidforged-merge`, HEAD `d3527705` ("feat(voidforged): port Voidforged systems (dynamic items, gear sets, reforge, chronal tribute, paragon)")
- **Source repo:** voidforged — branch `main`, HEAD `09e211b3` ("Rebalance the rift Facet augments")
- **Scope:** client-facing source under `Release-NMS-Client/**` only.

## 1. Repository identities and starting commits

| Repo | Path | Branch | HEAD | Working tree |
|---|---|---|---|---|
| Triptych-Triumvirate (destination) | `D:\Development\Triptych-Triumvirate` | `voidforged-merge` | `d3527705` | clean at task start (later: build-script repair + reports) |
| voidforged (source) | `D:\Development\voidforged` | `main` | `09e211b3` | clean |

- History: the repositories do **not** share object history (voidforged `09e211b3` is not present in Triptych's object store). Triptych is a separate repository whose `voidforged-merge` branch performed a prior port of the Voidforged systems.

## 2. Source-to-destination file comparison

- Compared client trees: voidforged `Release-NMS-Client` (527 tracked files) vs Triptych `Release-NMS-Client` (494 tracked files).
- File-set differences (source-level, excluding generated binaries/intermediates per task exclusions):
  - voidforged-only: `ClientFiles/dinput8.dll` (binary, excluded), `ClientFiles/uifiles/default/desktop.ini` (trivial).
  - Triptych-only: build scripts/logs, `ClientFiles/Resources/GlobalLoad*.txt`, NMS XML overlays, `eqgame_dll/nms_*` sources, `vault_window`, `core_achievements.h`, `stb_vorbis.c`, plus build artifacts.
- Content comparison of common files: **19 files differed**. In every case the Triptych side (`+` in diff) carried the integrated NMS/Voidforged implementation or a Triptych-specific enhancement; no substantive voidforged-only client functionality was found absent from the destination.

## 3. Files added, modified, and deleted

- Integration source changes made during this pass: **none** (integration state was already present in the destination working tree).
- Modified during this task (client scope): `Release-NMS-Client/build_eqgame_dll.bat` (portable `cd /d "%~dp0"`, `/t:Rebuild`, toolset `v145`).
- Added during this task: `docs/client-build-report.md`, `docs/VOIDFORGED_CLIENT_INTEGRATION_REPORT.md`.

## 4. Functionality successfully integrated

Client-side functionality integrated from the Voidforged lineage, present in Triptych's `Release-NMS-Client/eqgame_dll`:

- Shroud self-transform flow fix (OP_Shroud `0x6562`, plus `0x66B5`/`0x7A27`): byte patches (mov->NOP, jnz->jmp), AA pane scan bound widen, server-authoritative shroud state, hotbar restore, pose fix.
- Power-source (Energeian orb) support (OP_PowerSource `0x4C89`): DLL-owned opcode, native SetPower shim, diagnostics.
- Legends of Norrath: session login box close, session id handling, LoN pulse hooks.
- Loot protocol (`0x140A`/`0x140D`) and vault protocol/window integration.

## 5. Conflicts encountered and resolutions

- Shared files differed; Triptych implementations were preserved (e.g., `GetClassDesc` refactor with Warrior fallback; `LoadLibrary` -> `GetModuleHandleA` in `InitOffsets`; implemented `MQ2DataError`). No conflict required stopping for an unresolved architectural decision.

## 6. Triptych-specific functionality preserved

- Achievement row pool fix (`isAchievementRowPoolFixEnabled`), Triune of Fate naming, and other Triptych-side enhancements.

## 7. Functionality deliberately excluded

- voidforged `ClientFiles/dinput8.dll` binary, all generated build artifacts, server-side source, database, and VS temporary files.

## 8. Build results and DLL verification

- Authoritative target: `Release-NMS-Client/eqgame_dll.sln`, `Release | Win32` -> `Release-NMS-Client/Release/dinput8.dll`.
- Build: SUCCESS (MSBuild `/t:Rebuild /p:Configuration=Release /p:Platform=Win32 /p:WindowsTargetPlatformVersion=10.0.19041.0 /p:PlatformToolset=v145`).
- Verified: x86 (32-bit) PE32 DLL, Windows GUI subsystem, exports present; SHA-256 `7478654F375D55C1D53941139C322AAD8C3E7F646550987800F71FAC403E9E0C`; size 2,230,784 bytes.

## 9. Final Git working-tree status

- Modified: `Release-NMS-Client/build_eqgame_dll.bat`
- Untracked: `docs/client-build-report.md`, `docs/VOIDFORGED_CLIENT_INTEGRATION_REPORT.md`
- Build outputs under `Release-NMS-Client/Release/` are git-ignored.

## 10. Outstanding issues / recommendations

- Project-file default `WindowsTargetPlatformVersion` (`10.0.18362.0`) not installed; override `10.0.19041.0` used.
- `DirectInput8.sln`/`di-wrapper8` remains stale and excluded.
- Client source integration from voidforged appears already materialized in the destination; recommend a follow-up audit of any not-yet-ported voidforged client features.