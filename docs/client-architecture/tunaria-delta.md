# Tunaria Delta — Release-NMS-Client

Analyzed repository: D:\NMS Source
Analyzed component: Release-NMS-Client
Analyzed commit: d851cfd8830ad80361d85af5f12800a5837aa3e8
Analysis date: 2026-10-02
Reference baseline: Tunaria NMS-Release (C:\temp\NMS-Release-Tunaria\Release-NMS-Client)

---

## 1. Tree-Level Summary

| Metric | Count |
|---|---|
| Current tree files | 531 |
| Tunaria tree files | 597 |
| Shared identical (SHA-256 match) | 443 |
| Modified (differ vs Tunaria) | 14 |
| Current-only (not in Tunaria) | 4 |
| Tunaria-only (not in current) | 70 |

**No major structural reorganization.** The current tree is a direct descendant of the Tunaria baseline with a small, focused set of modifications. The overall directory layout, build system, and file organization are unchanged.

---

## 2. Exact Modified-File List (14 files)

The following 14 files differ from the Tunaria baseline (SHA-256 comparison):

1. `README.md`
2. `ClientFiles\uifiles\default\EQUI_Inventory.xml`
3. `eqgame_dll\core_eqg_load.h`
4. `eqgame_dll\neqgame.cpp`
5. `eqgame_dll\MQ2CleanUI.cpp`
6. `eqgame_dll\MQ2Commands.cpp`
7. `eqgame_dll\MQ2Globals.cpp`
8. `eqgame_dll\MQ2Labels.cpp`
9. `eqgame_dll\MQ2Pulse.cpp`
10. `eqgame_dll\MQ2Utilities.cpp`
11. `eqgame_dll\MQ2Windows.cpp`
12. `eqgame_dll\RenderHooks.h`
13. `eqgame_dll\waypoint_window.cpp`
14. `eqgame_dll\_options.h`

### Correction from prior audit

An earlier preliminary audit incorrectly listed `who_multiclass.cpp`, `pet_window.cpp`, and `multi_pet.cpp` as modified. SHA-256 comparison proves these three files are **bit-for-bit identical** to Tunaria:

- `who_multiclass.cpp` is identical to Tunaria
- `pet_window.cpp` is identical to Tunaria
- `multi_pet.cpp` is identical to Tunaria

The corrected modified set is exactly 14 files.

---

## 3. Semantic Delta for Each Modified File

### `README.md`

- **What changed:** Minor text tweaks.
- **Effect:** Documentation only; no runtime, build, or UI impact.
- **Scope:** Localized (non-code).

### `ClientFiles\uifiles\default\EQUI_Inventory.xml`

- **What changed:** Triune of Fate renames — `IW_EOM*` control names renamed to `IW_TriuneOfFate*` (lines ~3342–3367, 4051–4053).
- **Effect:** UI label/control naming for the Triune of Fate inventory feature. No layout or behavioral change.
- **Scope:** Localized UI customization.

### `eqgame_dll\core_eqg_load.h`

- **What changed:** Added null-check in EQG zone-loading override path.
- **Effect:** Prevents null-pointer dereference when EQG override is enabled but zone data is unavailable. Safety improvement.
- **Scope:** Localized safety fix.

### `eqgame_dll\neqgame.cpp`

- **What changed:** +10.8KB. Major additions:
  - Shroud `OP_0x6562` handling with pose/hotbar fixes (`eqgame.cpp:1017-1037`)
  - CAuth handshake `OP_CAuth` 0x7777 (`eqgame.cpp:1200-1250`)
  - Character-select multiclass parsing and state management (`eqgame.cpp:661-705`)
  - Opcode dispatch extensions in `HandleWorldMessage_Detour`
- **Effect:** Core NMS multiclass authentication, shroud flow fixes, and character-select multiclass display. This is the largest behavioral delta.
- **Scope:** Architectural — adds new packet handlers, new state globals (`g_cauth_bitmask`, `g_CharSelectInfo`), and new outbound packet synthesis.

### `eqgame_dll\MQ2CleanUI.cpp`

- **What changed:** Added call to `NMS_DestroyCustomWindows()` in `CleanGameUI`/`ReloadUI` detour paths; added `NMS_DrainCXStrAccess()` call.
- **Effect:** Ensures custom NMS windows are properly torn down and CXStr access state is drained during UI reload/cleanup.
- **Scope:** Localized lifecycle management.

### `eqgame_dll\MQ2Commands.cpp`

- **What changed:** Minor command table adjustment; `/filter mq2data` alias.
- **Effect:** Command registration only; no behavioral change to existing commands.
- **Scope:** Localized.

### `eqgame_dll\MQ2Globals.cpp`

- **What changed:** Removed `LoadLibrary` call from `DllMain` path; adjusted offset resolution.
- **Effect:** Eliminates a potential loader-lock issue during DLL attach. Build/initialization safety.
- **Scope:** Localized initialization change.

### `eqgame_dll\MQ2Labels.cpp`

- **What changed:** Content diff (size-equal, hash-different). NMS label callbacks including EQType 338 (Triune of Fate) and class abbreviation tables.
- **Effect:** Custom label rendering for NMS-specific inventory items and multiclass class names.
- **Scope:** Localized UI customization.

### `eqgame_dll\MQ2Pulse.cpp`

- **What changed:** +6.7KB. Major additions:
  - Shroud pose fix (`/sit+/stand`) handling
  - Hotbar restore scheduling after shroud
  - `gCXStrAccess` drain hooks (`NMS_DrainCXStrAccess()`)
  - `/who` detour paths
  - CAuth fire from `Heartbeat()` (`MQ2Pulse.cpp:351`)
  - Disc band maintenance (`MQ2Pulse.cpp:390-393`)
- **Effect:** Core heartbeat/pulse behavior for shroud, multiclass auth, and discipline bands.
- **Scope:** Architectural — new pulse-driven state management and packet synthesis.

### `eqgame_dll\MQ2Utilities.cpp`

- **What changed:** `MQ2DataError` implementation adjustment.
- **Effect:** Error reporting path only.
- **Scope:** Localized.

### `eqgame_dll\MQ2Windows.cpp`

- **What changed:** Duplicate-include guard in `GenerateMQUI()`; `WaypointsWnd::Initialize()` call added to `InitializeMQ2Windows()`.
- **Effect:** Prevents duplicate XML includes during MQUI generation; registers the waypoint window for initialization.
- **Scope:** Localized UI framework change.

### `eqgame_dll\RenderHooks.h`

- **What changed:** Added `NMS_DrainCXStrAccess()` call in `EndScene_Detour`; D3D9 hook coexistence adjustments.
- **Effect:** Ensures CXStr access state is drained every render frame.
- **Scope:** Localized render-path change.

### `eqgame_dll\waypoint_window.cpp`

- **What changed:** Taelosia category addition to waypoint list.
- **Effect:** Waypoint UI displays Taelosia zone category.
- **Scope:** Localized UI data.

### `eqgame_dll\_options.h`

- **What changed:** Single-line diff — `isOldModelHorseSupportEnabled` set to `true` (was already `true` in Tunaria's single-line diff context). Saylink and toggle-spellbook flags already present in Tunaria.
- **Effect:** No net runtime change relative to Tunaria baseline.
- **Scope:** Localized config.

---

## 4. Current-Only Files (4 files)

| File | Classification |
|---|---|
| `build_eqgame_dll_Debug.log` | Build log |
| `build_eqgame_dll_Release.log` | Build log |
| `ClientFiles\Resources\GlobalLoad.txt` | Generated client data |
| `ClientFiles\Resources\GlobalLoad_chr.txt` | Generated client data |

None of these are runtime dependencies of the compiled DLL. The build logs are local artifacts. The `GlobalLoad*.txt` files are consumed by the EQ client at runtime (model load lists) but are not referenced by the DLL itself.

---

## 5. Tunaria-Only Files (70 files)

Categorized by type:

| Category | Description |
|---|---|
| Build/prebuilt artifacts | `Release/` directory contents, `ClientFiles\dinput8.dll` (prebuilt binary), `desktop.ini` |
| Legacy project files | Old `.vcproj`, `.sln` variants no longer used |
| Removed source files | Files present in Tunaria but deleted in current tree |
| Documentation | Tunaria-specific docs not carried forward |

**Key highlight:** Many Tunaria-only files are prebuilt/build artifacts including `ClientFiles\dinput8.dll` — a stale prebuilt binary that should not be used (the committed `bin\Release\` prebuilts are stale and lack LAN customizations).

---

## 6. Provenance Conclusions

### Inherited from Tunaria (unchanged)

- All 443 identical files, including the entire MQ2/Edge infrastructure, detour framework, and the three NMS modules:
  - `who_multiclass.cpp` — identical to Tunaria
  - `pet_window.cpp` — identical to Tunaria
  - `multi_pet.cpp` — identical to Tunaria
- The `/who` 3-dword-vs-2-dword parse discrepancy (`who_multiclass.cpp:286-289` reads 3 uint32s before name; server serializes 2) is **inherited from Tunaria**, not introduced by current Triptych changes. Because `who_multiclass.cpp` is bit-for-bit identical to Tunaria, this anomaly predates the current tree.
- The `VFTABLE_CGaugeWnd = 0x9E87A8` unrebased absolute VA (`pet_window.cpp:56`) is inherited.
- The unbalanced `#pragma pack(push,1)`/`pop` in `eqgame.cpp:555-649` is inherited.
- The `InstallD3D9Hooks()` misleading name is inherited.
- The `ExitHooks()` never-called lifecycle gap is inherited.
- The `NMS_MapFilterWnd.xml` never-loaded dead asset is inherited.
- The `g_CharSelectBitmasks` orphaned extern is inherited.

### Current Triptych changes (14 modified files)

- **eqgame.cpp** — the primary delta: CAuth handshake, shroud handling, character-select multiclass parsing, opcode dispatch extensions.
- **MQ2Pulse.cpp** — the second major delta: shroud pose/hotbar fixes, drain hooks, CAuth fire, disc band maintenance.
- **MQ2Windows.cpp** — duplicate-include guard + waypoint window init.
- **MQ2CleanUI.cpp** — custom window teardown + drain.
- **RenderHooks.h** — drain on EndScene.
- **core_eqg_load.h** — null-check safety.
- **MQ2Globals.cpp** — LoadLibrary removal from DllMain path.
- **MQ2Labels.cpp** — Triune label callbacks.
- **MQ2Commands.cpp** — minor command table tweak.
- **MQ2Utilities.cpp** — error path tweak.
- **waypoint_window.cpp** — Taelosia category.
- **EQUI_Inventory.xml** — Triune of Fate renames.
- **_options.h** — no net runtime change.
- **README.md** — documentation.

### Inherited anomalies (not introduced by current changes)

The following anomalies exist in the current tree but are **inherited from Tunaria** because the relevant source files are identical:

1. `/who` parse discrepancy (3-vs-2 dword) — `who_multiclass.cpp` identical
2. `VFTABLE_CGaugeWnd` unrebased VA — `pet_window.cpp` identical
3. Unbalanced `#pragma pack` in `eqgame.cpp` — the pack pragmas are in the inherited portion
4. `InstallD3D9Hooks()` misleading name — inherited
5. `ExitHooks()` never called — inherited
6. `NMS_MapFilterWnd.xml` never loaded — inherited
7. `g_CharSelectBitmasks` orphaned extern — inherited

These are not "our bugs" — they are pre-existing in the Tunaria baseline.

---

**End of delta document.**
