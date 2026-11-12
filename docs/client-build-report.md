# Client Build Report — Triptych Triumvirate (NMS)

- **Date:** 2026-11-12
- **Branch:** `voidforged-merge`
- **Working-tree state at start:** clean; no pre-existing user changes were reverted, overwritten, or cleaned.
- **Scope:** client-facing deliverables only. No server-side source, database, or server build files were touched.

## 1. Client projects identified

| # | Project | Repo path | Type | Client-facing deliverable |
|---|---------|-----------|------|---------------------------|
| 1 | `eqgame_dll` | `Release-NMS-Client/eqgame_dll.sln` -> `Release-NMS-Client/eqgame_dll/eqgame_dll.vcxproj` (project name `eqgame_dll_rof2`) | C++ DLL (DynamicLibrary, Win32) | **Primary - builds `dinput8.dll`** (DirectInput8 proxy add-on for the RoF2/EverQuest client) |
| 2 | `DirectInput8` / `di-wrapper8` | `Release-NMS-Client/eqgame_dll/msvc/DirectInput8.sln` -> `Release-NMS-Client/eqgame_dll/msvc/di-wrapper8.vcxproj` | C++ DLL (wrapper) | Secondary/legacy - not part of the active NMS build path; **failed** (see section 4) |

Client data assets that are overlay resources, not compiled deliverables:

- `Release-NMS-Client/ClientFiles/` - UI overlay (`uifiles/**/*.xml`, `Resources/GlobalLoad.txt`, `Resources/GlobalLoad_chr.txt`)
- `Release-NMS-Client/SpellIcons/` - spell icon PNGs
- `Release-NMS-Client/Blech/` - local test scaffolding (not included in the project)

Server-side projects (CMake: `Release-NMS-Server/`) were identified for exclusion and were **not** built.

## 2. Build configuration used

- Solution: `Release-NMS-Client/eqgame_dll.sln`
- Project: `eqgame_dll`
- Configuration: `Release`
- Platform: `Win32` (x86 - RoF2 client is 32-bit; x64 will not load)
- Toolset: `v145` (installed MSVC toolset `14.51.36231`, i.e. Visual Studio 18 / 2026)
- Windows SDK: `10.0.19041.0` (override; project default `10.0.18362.0` is not installed on this machine)
- Subsystem: Windows GUI; Dynamic base; NX compatible
- Module definition file: `Release-NMS-Client/eqgame_dll/dinput8.def`

## 3. Build commands executed

1. Direct MSBuild invocation (corrected overrides):

```bat
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" ^
  "D:\Development\Triptych-Triumvirate\Release-NMS-Client\eqgame_dll.sln" ^
  /t:Rebuild /p:Configuration=Release /p:Platform=Win32 ^
  /p:WindowsTargetPlatformVersion=10.0.19041.0 /p:PlatformToolset=v145 /m /v:m /nologo
```

2. Repository build script (after repair), via `cmd /c`:

```bat
D:\Development\Triptych-Triumvirate\Release-NMS-Client\build_eqgame_dll.bat
```

## 4. Results

### Successful target

- **`eqgame_dll` - "builds `dinput8.dll`": BUILD SUCCEEDED** (MSBuild exit code 0; full rebuild completed; link step reported `eqgame_dll.vcxproj -> ...\Release\dinput8.dll`).

### Unsuccessful target(s)

- **`DirectInput8.sln` / `di-wrapper8`:** BUILD FAILED (MSBuild exit code 1) -
  `error C1083: Cannot open include file: 'd3d_example.hpp' [D:\Development\Triptych-Triumvirate\Release-NMS-Client\eqgame_dll\msvc\di-wrapper8.vcxproj]`.
  The wrapper project file references source/header paths that no longer exist in the current tree; it is not part of the active NMS client build path and was excluded from the deliverable set.

## 5. Generated artifacts and verification

- **Binary:** `D:\Development\Triptych-Triumvirate\Release-NMS-Client/Release/dinput8.dll`
- **Supporting linker artifacts:** `Release-NMS-Client/Release/dinput8.lib`, `Release-NMS-Client/Release/dinput8.exp` (nms_vorbis)
- **SHA-256:** `7478654F375D55C1D53941139C322AAD8C3E7F646550987800F71FAC403E9E0C`
- **Size:** 2,230,784 bytes
- **Verified via dumpbin:**
  - File Type: DLL
  - Machine: `14C` (x86) / PE32 / 32-bit word machine - i.e. **x86 (32-bit)**, as required by the RoF2 client
  - Subsystem: Windows GUI
  - Exports present and expected (from `dinput8.def`): `DirectInput8Create`, `DllCanUnloadNow`, `DllGetClassObject`, `DllRegisterServer`, `DllUnregisterServer`, `GetdfDIJoystick`

## 6. Source changes made during this build

Only one file was modified (client scope only):

- `Release-NMS-Client/build_eqgame_dll.bat`
  - `cd /d "C:\EQS\Release-NMS-Client"` -> `cd /d "%~dp0"` (portable to the actual checkout location)
  - Added `/t:Rebuild` (documented full-rebuild requirement)
  - `/p:PlatformToolset=v143` -> `/p:PlatformToolset=v145` (installed toolset on this machine)

No C++ source, project, server, database, or configuration files were changed.

## 7. Outstanding issues / notes

- Project-file default `WindowsTargetPlatformVersion` (`10.0.18362.0`) is not installed; the build was run with the override `10.0.19041.0`.
- Non-fatal warnings during build (pre-existing): `C4091` (static in MQ2 member structs), `C4200` (zero-sized array), `C4005` (`_WIN32_WINNT` redefinition), `LNK4099` (detours PDB absent), `LINK : /LTCG specified but no code generation required`.
- Client assets verified present (overlay data, not compiled): required `ClientFiles/uifiles/default/NMS_*.xml` windows, `EQUI_PetInfoWindow.xml`, `EQUI_Inventory.xml`, `EQUI_BazaarSearchWnd.xml`, `EQUI_CharacterListWnd.xml`, `EQUI_MerchantWnd.xml`, and `Resources/GlobalLoad*.txt`.
- Nothing was deployed to a live client installation.

## 8. Final state

- Successful deliverable: `Release-NMS-Client/Release/dinput8.dll` (SHA-256 `7478654F375D55C1D53941139C322AAD8C3E7F646550987800F71FAC403E9E0C`)
- Modified files: `Release-NMS-Client/build_eqgame_dll.bat`
- Generated artifacts: `Release-NMS-Client/Release/dinput8.dll`, `dinput8.lib`, `dinput8.exp` (git-ignored build outputs)