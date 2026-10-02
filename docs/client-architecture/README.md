# NMS Client Architecture

Analyzed commit: d851cfd8830ad80361d85af5f12800a5837aa3e8
Analysis date: 2026-10-02
Target client: RoF2 (Ring of Fire 2)
Tunaria reference: Tunaria NMS-Release

---

## 1. Purpose

Release-NMS-Client is a DirectInput8 proxy DLL (`dinput8.dll`) that injects custom behavior into the RoF2 EverQuest client. It provides multiclass UI presentation, multi-pet support, waypoints, shroud handling, discipline timer bands, and targeted packet interception. The DLL is an overlay — it does not replace `eqgame.exe`.

---

## 2. Mental Model

The system has seven layers:

1. **DirectInput8 proxy/bootstrap** — Forwards six DI exports to the system `dinput8.dll`; wraps COM interfaces with hook classes; calls `InitHooks()` from `DllMain`.
2. **EQ memory binding layer** — Resolves preferred-base addresses (`0x400000`) to runtime addresses; binds ~3170 `FUNCTION_AT_ADDRESS` entry points; mirrors EQ struct layouts.
3. **Hook/detour layer** — Detours-based function interception, vtable patching, D3D9 device hooks, direct memory patches.
4. **MQ2-derived infrastructure** — Window/XML injection framework, pulse/heartbeat loop, command registration, map plugin scaffolding (subset active).
5. **NMS feature layer** — Multiclass/CAuth, `/who` reformatting, MultiPet, PetWindow, Waypoints, Shroud fixes, Disc bands, UI customizations.
6. **Client/server protocol layer** — Opcode interception and serialization; custom contracts (`0x1338`, `0x1341`, `0x1402`, `0x1403`, `0x7777`, `0x578c`, `0x6562`); field repurposing (deity→bitmask, Class_→bitmask).
7. **UI/render layer** — XML generation (`MQUI.xml`), custom SIDL windows, floating text manager (D3D9), labels.

---

## 3. Startup Sequence

```
eqgame.exe loads dinput8.dll
  → DllMain(DLL_PROCESS_ATTACH)
    → loads system dinput8.dll, resolves 6 exports
    → InitHooks()
      → InitOffsets / GetEQPath / gDetourCS
      → MQ2 init block (gated by isMQInjectsEnabled)
      → InitOptions() [applies _options.h]
      → unconditional detours installed
      → option-gated patches applied
      → NMS subsystems initialized (MultiPet, PetWindow, WhoMulticlass)
    → DI export forwarding active

Deferred:
  First ProcessGameEvents → InstallD3D9Hooks() [~24 gameplay detours + D3D9]
  First ingame pulse → vtable hooks (CombatAbility, Tracking)
  OP_ServerAuthStats → g_cauth_bitmask set
  UdpSend → g_zone_udp_con captured
  Heartbeat() → FireCAuthHandshake() when ready
```

---

## 4. Runtime Reachability

| Category | Description |
|---|---|
| **Active** | DI proxy, memory bindings, unconditional detours, NMS subsystem init, pulse/heartbeat |
| **Conditional** | Option-gated patches (saylink, gamma, old-model horses, patchme bypass) |
| **Packet-triggered** | `0x1338` (auth stats), `0x578c` (who), `0x1341` (pet list), `0x1402` (waypoint list), `0x6562` (shroud), `0x4513/0x00d2` (char select) |
| **Pulse-triggered** | Disc band maintenance, spoofs, shroud follow-ups, CAuth fire, drain |
| **Render-triggered** | FTM init, D3D9 Reset hook, `NMS_DrainCXStrAccess()` |
| **Compiled but unreachable** | MQ2 data-type/macro/parser subsystem, `ExitHooks()`, `UdpRoutePacket_Detour`, `MQ2Initialize()`/`MQ2Start()` |
| **Present but not compiled** | `MQ2Main.cpp`, `MQ2AdvDps/MMODPS/KeyBinds/Benchmarks/DInput/FloatingDamage`, `ISXEQ/*`, `dllmain.cpp`, proxy stubs |

---

## 5. Active Feature Map

| Feature | Client entry | Key files |
|---|---|---|
| Multiclass/CAuth | `FireCAuthHandshake()` | `eqgame.cpp`, `MQ2Pulse.cpp` |
| Character select | `ProcessNMSCharSelect` | `eqgame.cpp` |
| Multiclass `/who` | `WhoMulticlass::OnIncomingMessage` | `who_multiclass.cpp` |
| MultiPet | `MultiPet::OnIncomingMessage` | `multi_pet.cpp` |
| PetWindow | `PetWindow::Initialize` | `pet_window.cpp` |
| Waypoints | `WaypointsWnd::Initialize` | `waypoint_window.cpp` |
| Shroud handling | `HandleWorldMessage` branch | `eqgame.cpp`, `MQ2Pulse.cpp` |
| Discipline bands | `Heartbeat()` maintenance | `Hooks.cpp`, `MQ2Pulse.cpp`, `nms_disc_bands.h` |
| Floating text/render | `InitializeFloatingTextPlugin()` | `FloatingTextManager.cpp`, `RenderHooks.h` |
| Triune/inventory UI | Label callbacks (EQType 338) | `MQ2Labels.cpp`, `EQUI_Inventory.xml` |

---

## 6. Server Dependency Map

| Feature | Requires Triptych server behavior |
|---|---|
| CAuth | Server sends `statClassesBitstats` (key 1) in `OP_ServerAuthStats`; verifies `OP_CAuth` response |
| Multiclass `/who` | Server places `GetClassesBits()` into `Class_` field of WhoAll entries |
| MultiPet | Server sends `OP_PetList` (0x1341) with spawn_id/class_id pairs |
| Waypoints | Server sends `OP_WaypointList` (0x1402); handles `OP_WaypointRequest` (0x1403) |
| Shroud | Server sends 20472-byte profile block with identity offsets |
| Disc bands | Server uses banded IDs (>=20) for multiclass discipline timers |
| Char-select multiclass | Server writes `pp.classes` into `cse->Deity` (repurposed field) |

---

## 7. UI/Data Dependencies

**Required XML files:**
- `ClientFiles/uifiles/default/NMS_WaypointsWnd.xml`
- `ClientFiles/uifiles/default/EQUI_Inventory.xml`
- `ClientFiles/uifiles/default/EQUI_PetInfoWindow.xml`
- `ClientFiles/uifiles/default/EQUI_BazaarSearchWnd.xml`
- `ClientFiles/uifiles/default/EQUI_CharacterListWnd.xml`
- `ClientFiles/uifiles/default/EQUI_MerchantWnd.xml`

**Generated client data (consumed by EQ, not DLL):**
- `ClientFiles/Resources/GlobalLoad.txt`
- `ClientFiles/Resources/GlobalLoad_chr.txt`

**DB-derived exports (deploy-time via `export-client-files.bat`):**
- `spells_us.txt`, `dbstr_us.txt`, `SkillCaps.txt`, `BaseData.txt`

---

## 8. Architectural Fragility Map

| Category | Description |
|---|---|
| Hard-coded addresses | ~3170 `FUNCTION_AT_ADDRESS` bindings at preferred-base VAs; uniform `X-0x400000+baseAddress` rebasing |
| Client-version dependency | `eqgame.cpp:2101-2110` aborts if `__ExpectedVersionDate/Time` mismatches runtime |
| pack(1) | Global `/Zp1` + `#pragma pack(push,1)` on wire structs; unbalanced push/pop in `eqgame.cpp:555-649` |
| Render-gated gameplay hooks | ~24 gameplay detours installed inside `InstallD3D9Hooks()` — depend on D3D9 device probe succeeding |
| Field repurposing | Character-select deity field carries class bitmask; WhoAll `Class_` field carries bitmask |
| Incomplete shutdown | `ExitHooks()` never called; `bInitalized` never set true |
| Unrebased VA | `VFTABLE_CGaugeWnd = 0x9E87A8` (`pet_window.cpp:56`) — only address not using rebase idiom |
| Dormant init path | `MQ2Initialize()`/`MQ2Start()` duplicate init but unreachable — risk if ever invoked |

---

## 9. Known Structural Anomalies

See [forensic-dossier.md](forensic-dossier.md) §20 for the full list. Summary:

1. `VFTABLE_CGaugeWnd` unrebased absolute VA
2. `/who` parse discrepancy (3-vs-2 dword) — **inherited from Tunaria**
3. `InstallD3D9Hooks()` misleading name
4. Unbalanced `#pragma pack(push,1)`/`pop`
5. `g_CharSelectBitmasks` orphaned extern
6. `ExitHooks()` never called
7. `MQ2Initialize()`/`MQ2Start()` unreachable
8. `NMS_MapFilterWnd.xml` never loaded
9. Skins not in sync
10. Resources missing DB-derived client files
11. `0x7EBE` caster mask literal undocumented
12. `VFTABLE_CGaugeWnd` ASLR/relocation sensitivity

---

## 10. Documentation Map

| Document | Purpose |
|---|---|
| [forensic-dossier.md](forensic-dossier.md) | Full 22-section forensic architecture dossier with complete evidence |
| [tunaria-delta.md](tunaria-delta.md) | Structural comparison with Tunaria baseline; modified-file provenance |
| [protocol-contracts.md](protocol-contracts.md) | Canonical client/server opcode contract reference |

---

## 11. ASCII Architecture Diagram

```
eqgame.exe
  → dinput8.dll (proxy)
    → memory bindings (offsets, FUNCTION_AT_ADDRESS)
      → hook infrastructure (Detours, vtable, D3D9)
        → NMS feature layer
          ↔ Triptych server
            → UI/gameplay result

Branches:
  Packet:   HandleWorldMessage → dispatch → suppress/mutate/observe
  Pulse:    ProcessGameEvents → Heartbeat → spoofs, CAuth, disc bands
  Render:   InstallD3D9Hooks → BeginScene/EndScene → FTM, drain
  UI/XML:   XMLRead → GenerateMQUI → inject NMS windows
```

---

**End of README.**
