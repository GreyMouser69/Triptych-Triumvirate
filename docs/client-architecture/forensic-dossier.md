# NMS Client Forensic Architecture Dossier

Analyzed repository: D:\NMS Source
Analyzed component: Release-NMS-Client
Analyzed commit: d851cfd8830ad80361d85af5f12800a5837aa3e8
Analysis date: 2026-10-02
Reference baseline: Tunaria NMS-Release

Analysis sources:
- Big Pickle semantic/reachability architecture investigation
- Supporting Tunaria structural comparison
- Server source consulted only where necessary to establish client/server contracts

Status:
- Read-only forensic snapshot
- Describes the repository state at the anchored commit
- Findings may become stale after future source changes

Working tree note:
- quest-script-reference.md was intentionally untracked and excluded from this analysis

---

## 1. SYSTEM PURPOSE AND BOUNDARIES

**What `dinput8.dll` is.** The NMS client modification is a DirectInput8 proxy DLL that injects custom behavior into the RoF2 EverQuest client. The compiled binary (`dinput8.dll`) is loaded by `eqgame.exe` and replaces the system's DirectInput entry points via export forwarding while also installing runtime detours into the EQ process.

**Why EverQuest loads it.** The RoF2 client loads `dinput8.dll` at runtime to satisfy DirectInput 8 creation. By placing a custom `dinput8.dll` in the EQ client directory or by load-order hijacking, the process loads this DLL first. Its exports (see §2) allow EQ to obtain `IDirectInput8*` while the proxy simultaneously hooks critical game functions.

**What is proxied to the real system `dinput8.dll`.** Six exports are forwarded to `C:\Windows\System32\dinput8.dll`: `DirectInput8Create`, `DllCanUnloadNow`, `DllGetClassObject`, `DllRegisterServer`, `DllUnregisterServer`, `GetdfDIJoystick` (`dinput8.def:1-9`; `eqgame.cpp:2273-2284`, `:2296-2358`). On successful `DirectInput8Create`, `genericQueryInterface` wraps the returned COM interfaces with the hook classes (`IDirectInput8Hook`, `IDirectInputDevice8Hook`) via `ProxyAddressLookupTable` (`eqgame.cpp:2233`, `IDirectInput8Hook.cpp:1-76`).

**DLL vs stock EQ.** This DLL is an overlay: it does not replace `eqgame.exe`. Stock EQ behavior is preserved unless overridden by detours/patches. Custom behavior includes multiclass presentation, multi-pet UI/logic, waypoints, shroud handling, discipline timer bands, AA/skill gating, UI XML injection, and targeted packet interception.

**Server vs client.** The server is authoritative for game state (multiclass bitmask, pet lists, waypoints, shroud profiles, auth). The client intercepts and reformats certain packets (`0x578c`, `0x1341`, `0x1402`) to render NMS-specific UI; it synthesizes CAuth (`0x7777`) using server-provided `statClassesBitmask` and enforces client-side UX (disc band mapping, spell/skill availability hints). Protocol details in §8.

**Inherited MQ2/Edge vs genuinely used by NMS.** The codebase is MQ2 (GPL) forked to Edge, then adapted to classless-dll/NMS. Compiled and actively used: core detour/pulse infrastructure, command registration for `/who,/fov,/camera`, XML/window framework, render hooks for floating text, and the NMS modules (`who_multiclass`, `multi_pet`, `pet_window`, `waypoint_window`). Compiled but unreachable: the MQ2 data-type/macro/parser subsystem (`MQ2Data*`, `MQ2ParseAPI`, `MQ2MacroCommands`, `MQ2Mouse`, `MQ2MapCommands`) — never initialized (see §12). Present but not compiled: `MQ2Main.cpp`, `MQ2AdvDps/MMODPS/KeyBinds/Benchmarks/DInput/FloatingDamage`, `ISXEQ/*`, `msvc/*`, proxy stubs (`dllmain.cpp`, `proxy_idi/idid.cpp`).

**Compiled but inactive.** Examples: `ExitHooks()` (`eqgame.cpp:2071-2081`, no callers, guarded by never-set `bInitalized`), `MQ2Initialize()`/`MQ2Start()` (`:2120-2227`, no callers), `UdpRoutePacket_Detour` installed site commented out (`:1683-1685`), `InitializeParser` never called, `areDefaultShieldsIgnored` only read behind never-installed shield detour.

**Repository artifacts not compiled.** Numerous legacy `.cpp/.h`, project files (`MQ2Main.*`, `ISXEQ.vcproj`, `msvc/*`), and build outputs are retained for provenance; they do not participate in the build.

## 2. BUILD AND BINARY ARCHITECTURE

**Solution/project.** `eqgame_dll.sln:4-15` contains exactly one project: `eqgame_dll\neqgame_dll.vcxproj`. Configurations: `Debug|Win32`, `Release|Win32`. Project name `eqgame_dll_rof2` in the older form; output `dinput8.dll`.

**Compiled source set (49 TUs).** See `<ClCompile>` in `eqgame_dll.vcxproj:122-170`: includes `eqgame.cpp`, `Hooks.cpp`, `EQClasses.cpp`, `EQUtils.cpp`, `FloatingTextManager.cpp`, `FPSLimit.cpp` (0B), `DetoursAPI.cpp` (0B), all `IDirectInput*` family, `InterfaceQuery.cpp`, `IClassFactory.cpp`, `MQ2ChatHook.cpp`, `MQ2CleanUI.cpp`, `MQ2CommandAPI.cpp`, `MQ2Commands.cpp`, `MQ2Data.cpp`, `MQ2DataAPI.cpp`, `MQ2DataTypes.cpp`, `MQ2DataVars.cpp`, `MQ2DetourAPI.cpp`, `MQ2FloatingText.cpp`, `MQ2Globals.cpp`, `MQ2ItemDisplay.cpp`, `MQ2Labels.cpp`, `MQ2MacroCommands.cpp`, `MQ2Map.cpp`, `MQ2MapAPI.cpp`, `MQ2MapCommands.cpp`, `MQ2Mouse.cpp`, `MQ2ParseAPI.cpp`, `MQ2PluginHandler.cpp`, `MQ2Pulse.cpp`, `MQ2Spawns.cpp`, `MQ2Utilities.cpp`, `MQ2Windows.cpp`, `multi_pet.cpp`, `pet_window.cpp`, `Sprite.cpp`, `Text.cpp`, `waypoint_window.cpp`, `who_multiclass.cpp`.

**Toolset/assumptions.** Platform Toolset `v142` (VS2019), `WindowsTargetPlatformVersion 10.0.18362.0`, `CharacterSet MultiByte`, `ConfigurationType DynamicLibrary`, Win32 (x86) only. Runtime: `MultiThreaded` (Release), `MultiThreadedDebugDLL` (Debug). Optimization disabled in Release per project (`Optimization Disabled`, `WholeProgramOptimization false`) — intentional for detours/patch stability. Struct alignment `1Byte` (`/Zp1`) at `vcxproj:65,98`.

**Packing/alignment.** `#pragma pack(push,1)`/`pack(pop)` used extensively: `waypoint_window.cpp:3-27`, `eqgame.cpp:555-649` (two pushes, one pop), `dinputproxy.h:5`, `MQ2Main.h:64-67`. Global `pack(1)` affects wire structs (load-bearing). See §20 anomaly #4.

**Exports/linker.** `ModuleDefinitionFile dinput8.def` (`vcxproj:82,116`); exports six symbols as above. No `dllexport` decoration used for the DI exports (def-driven). Additional dependencies: Detours headers/libs (`..\detours\inc`, `..\Detours\lib`), DX9 (`..\dependencies\dx9\Include`, `..\dependencies\dx9\Lib`), Boost headers present.

**Build-time flags.** `DINPUT8_EXPORTS`, `EQLIB_EXPORTS`, `DPSPLUGIN`, `WINDOWS_IGNORE_PACKING_MISMATCH`. `__ClientOverride 0` (`eqgame.h:24`), `NMS_SHROUD_DIAG 0` (`eqgame.cpp:785-786`). `ISXEQ/TESTMEM` undefined.

**Binary identity.** Output `dinput8.dll` (Win32). Project name `eqgame_dll_rof2`.

**Dependency diagram (concise):** `eqgame.exe` → loads `dinput8.dll` (this) → loads system `dinput8.dll` → forwards DI exports; this DLL detours EQ functions at fixed RVAs (RoF2 build), uses Detours, hooks D3D9 vtable (optional render path), reads EQ memory via struct mirrors.

## 3. DLL LOAD AND BOOTSTRAP SEQUENCE

Chronological order (verified):

1. `eqgame.exe` loads `dinput8.dll` (this module). Windows calls `DllMain` with `DLL_PROCESS_ATTACH`.
2. `DllMain` at `eqgame.cpp:2241-2294`: reads `dwReason==DLL_PROCESS_ATTACH`. Sets CWD to EQ folder (`:38-47`) — forces relative paths to resolve. Loads system DI: `GetSystemDirectoryA` + `\dinput8.dll`, `LoadLibraryA` → stores `dinput8dll` handle (`:247-2251`). Stores module/instance (`:2254-2255`). Parses paths (`:2257-2267`). Calls **`InitHooks()`** (`eqgame.cpp:2267`).
3. `InitHooks()` `eqgame.cpp:1589-2068`: `InitOffsets()` (`:1595`), `GetEQPath` (`:1596`), `InitializeCriticalSection(&gDetourCS)` (`:1597`). If `isMQInjectsEnabled` (true): calls `InitializeMQ2Detours()` (`:1600`), `InitializeDisplayHook()` (`:1601`), `InitializeChatHook()` (`:1602`), `InitializeMQ2Commands()` (`:1603`), `InitializeMQ2Windows()` (`:1604`), `InitializeMQ2Pulse()` (`:1605`), `InitializeMQ2Spawns()` (`:1606`), `InitializeMapPlugin()` (`:1607`), `InitializeMQ2ItemDisplay()` (`:1608`), `InitializeMQ2Labels()` (`:1609`). Guards `if (!baseAddress) return;` (`:1641`) — effectively dead (static init non-null). Calls `InitOptions()` (`:1643`, applies `_options.h` via `core_init.h:11-20`).
4. Patches applied: AA-at-level-1 (`:1646-1663`), unconditional detours installed: `SendMessage` (`0x8C4CE0`, `:1666`), `HandleWorldMessage` (`0x4C3250`, `:1669`), `ApplyShroudFlowFix` (`:1673`), `UdpSend` (`0x8C51F0`, `:1677`), `WorldRoutePacket` (`0x5629C0`, `:1681`), `GetClassDesc` (`:1688`). Saylink8 and other option-gated patches (`:1705-1969`), gamma detours if enabled (`:1971-1980`).
5. NMS subsystems initialized: `g_multiPet.Initialize()` (`:2065`), `g_petWindow.Initialize()` (`:2066`), `g_whoMulticlass.Initialize()` (`:2067`).
6. Control returns to `DllMain`. Resolves DI export addresses (`:2273-2284`) and stores function pointers. `DLL_PROCESS_ATTACH` complete (returns true).
7. Deferred initialization: D3D9 hooks installed on first `ProcessGameEvents` call via `InitializeFloatingTextPlugin()` → `InstallD3D9Hooks()` (`MQ2Pulse.cpp:802-803`, `MQ2FloatingText.cpp:85`, `Hooks.cpp:1857`). Vtable hooks (combat/tracking) applied on first ingame pulse (`MQ2Pulse.cpp:311,317-318`). CAuth state captured on first `OP_ServerAuthStats` (`eqgame.cpp:1041-1053`) and `UdpSend` sees zone connection (`:1203-1210`); handshake fires from `Heartbeat()` (`MQ2Pulse.cpp:351`) when ready.
8. Shutdown: `DllMain` handles `DLL_PROCESS_DETACH` by calling `CoUninitialize()` and `FreeLibrary(dinput8dll)` (`eqgame.cpp:2287-2290`). `ExitHooks()` (`:2071-2081`) is never called (no caller; `bInitalized` never set true).

## 4. HOOKING ARCHITECTURE

### A. Packet/network hooks (active)

| Target | Installed | Technique | Purpose | Gated |
|---|---|---|---|---|
| `HandleWorldMessage` `0x4C3250` | `eqgame.cpp:1669` | `EzDetour` | Master inbound dispatcher (0x6562, 0x1338, 0x1340, 0x4513/0x00d2, 0x578c, 0x1341, 0x1402, 0x6b6d, 0x6989 pass-through) | unconditional |
| `WorldRoutePacket` `0x5629C0` | `eqgame.cpp:1681` | `EzDetour` | Login/world auth path (0x1340, 0x4513/0x00d2) | unconditional |
| `UdpConnection::Send` `0x8C51F0` | `eqgame.cpp:1677` | `EzDetour` | Captures real zone UDP connection into `g_zone_udp_con` | unconditional |
| `UdpConnection::OnRoutePacket` `0x8C5070` | **commented** `eqgame.cpp:1685` | (disabled) | Legacy path; body compiled but not installed | disabled |
| `SendMessage` `0x8C4CE0` | `eqgame.cpp:1666` | `EzDetour` | Outbound UI→world (AA Train-All rewrite, waypoint requests, checksum scramble path) | unconditional |

### B. Game-loop/pulse hooks (active)

| Target | Installed | Technique | Purpose | Gated |
|---|---|---|---|---|
| `ProcessGameEvents` | `MQ2Pulse.cpp:867` | `EzDetour` | Heartbeat entry; triggers FTM init on first call | `isMQInjectsEnabled` |
| `CEverQuest::EnterZone` | `MQ2Pulse.cpp:868` | `EzDetour` | Re-arms `g_cauth_sent` on zone change | `isMQInjectsEnabled` |
| `CEverQuest::SetGameState` | `MQ2Pulse.cpp:869` | `EzDetour` | Dispatches state changes to feature mods | `isMQInjectsEnabled` |
| `CombatAbilityWnd` vtable slot 49 | `MQ2Pulse.cpp:311` | `hook_vtable_direct` | Frame callback for CA window | `isMQInjectsEnabled` |
| `TrackingWnd` vtable slots 49,34 | `MQ2Pulse.cpp:317-318` | `hook_vtable_direct` | Frame + notification | `isMQInjectsEnabled` |

### C. Rendering hooks (active, deferred)

| Target | Installed | Technique | Purpose | Gated |
|---|---|---|---|---|
| `IDirect3DDevice9::BeginScene` (vtable 0x29) | `Hooks.cpp:1893` | `InstallDetour` | Captures device, one-time init | via `InstallD3D9Hooks()` (lazy) |
| `IDirect3DDevice9::EndScene` (vtable 0x2A) | `Hooks.cpp:1900` | `InstallDetour` | Renders FTM, calls `NMS_DrainCXStrAccess()` every frame | lazy |
| `IDirect3DDevice9::Reset` (vtable 0x10) | `RenderHooks.h:52-55` | `InstallDetour` | Device lost/reset recovery | lazy |

### D. UI/window hooks (active)

| Target | Installed | Technique | Purpose | Gated |
|---|---|---|---|---|
| `CDisplay::CleanGameUI` | `MQ2CleanUI.cpp:187` | `EzDetour` | Tears down custom windows | `isMQInjectsEnabled` |
| `CDisplay::ReloadUI` | `MQ2CleanUI.cpp:188` | `EzDetour` | Rebuilds custom windows | `isMQInjectsEnabled` |
| `EQ_LoadingS::SetProgressBar` | `MQ2CleanUI.cpp:193` | `EzDetour` | Progress UI hook | `isMQInjectsEnabled` |
| `CXMLSOMDocumentBase::XMLRead` | `MQ2Windows.cpp:238` | `EzDetour` | Rewrites `EQUI.xml` → `MQUI.xml` | `isMQInjectsEnabled` |
| `CSidlScreenWnd::Init1` | `MQ2Windows.cpp:239` | `EzDetour` | SIDL init | `isMQInjectsEnabled` |
| `CXWndManager::RemoveWnd` | `MQ2Windows.cpp:240` | `EzDetour` | Window removal tracking | `isMQInjectsEnabled` |
| `CMapViewWnd` ctor + vtable steal | `MQ2Map.cpp:277-278` | `EzDetour` + full vtable replacement | Map plugin integration | `isMQInjectsEnabled` |

### E. Command/chat hooks (active)

| Target | Installed | Technique | Purpose | Gated |
|---|---|---|---|---|
| `CEverQuest::dsp_chat`, `__DoTellWindow`, `__UPCNotificationFlush` | `MQ2ChatHook.cpp:159-161` | `EzDetour` (3) | Chat interception/suppression | `isMQInjectsEnabled` |
| `CEverQuest::InterpretCmd` | `MQ2CommandAPI.cpp:576` | `EzDetour` | Command dispatch base | `isMQInjectsEnabled` |
| `ExecuteCmd` | `Hooks.cpp:2211` | `InstallDetour` | Command path hook | via D3D9 init path |
| `cmd_alternateadv` | `Hooks.cpp:2225` | `InstallDetour` | `/alt toggle` → `#alttoggle` | via D3D9 init path |

### F. Direct memory patches (active)

Key patches: AA-at-level-1 (4) `eqgame.cpp:1649-1662`; Saylink8 (9) `:1800-1819`; Shroud flow fix A/B (self-verifying memcmp) `:818-841`; old-model horses `:1728-1729`; patchme bypass `:1789-1790`; gamma detours installed separately. Discipline/merchant/spell gates are detours (§10), not byte patches.

### G. Dead/unreachable legacy MQ2 hooks

`UdpRoutePacket_Detour` body exists (`eqgame.cpp:1174-1187`) but install site commented (`:1683-1685`). `MQ2Initialize()`/`MQ2Start()` path duplicates init but never called. `InitializeParser`/`InitializeMQ2Data*`/`InitializeMQ2KeyBinds` never invoked. `ShutdownHooks` only referenced by uncompiled `MQ2FloatingDamage.cpp:146`. `ExitHooks()` never called.

## 5. HARDCODED CLIENT MEMORY MODEL

**Base assumptions.** Preferred load base `0x400000`. `baseAddress = (DWORD)GetModuleHandle(NULL);` (`MQ2Globals.cpp:30`, static init). All rebased addresses follow `((X - 0x400000) + baseAddress)` idiom (hundreds of sites). One deliberate exception: `VFTABLE_CGaugeWnd = 0x9E87A8` (`pet_window.cpp:56`) — raw absolute VA with no rebasing (anomaly #1, §20).

**FUNCTION_AT_ADDRESS.** ~3170 `FUNCTION_AT_ADDRESS` bindings (`MQ2Main.h:131-136`, `EQClasses.cpp` bulk) emit `__declspec(naked) { mov eax, <absolute VA>; jmp eax; }`. These are absolute preferred-base VAs; relocation depends on PE fixups for the module. This is the primary address binding mechanism for EQ classes/functions.

**Offset/header organization.** `eqgame.h:21-695` defines ~530 `*_x` preferred-base constants. `MQ2Globals.cpp:1130+` expands `INITIALIZE_EQGAME_OFFSET()` macro mapping each to a runtime `DWORD var = ((var##_x - 0x400000) + baseAddress)`. `EQData.h` contains ~912 offset-commented fields and 312 `#define`s. `EQClasses.h` mirrors class layouts.

**Global pointers.** `baseAddress`, `eqGraphicsAddress`, `eqMainAddress` (`MQ2Globals.cpp:28-31`), `g_pDevice` (`RenderHooks.h:29`), `g_zone_udp_con` (`eqgame.cpp:26`), `g_cauth_bitmask` (`:33`), `g_CharSelectInfo[250]`/`count` (`:657-658`), `g_pFtm` singleton (`FloatingTextManager.cpp:9`), `g_multiPet`/`g_petWindow`/`g_whoMulticlass` file-scope statics (`eqgame.cpp:28-30`).

**VFTABLE constants.** D3D9 slots 0x10 (Reset), 0x29 (BeginScene), 0x2A (EndScene). Window vtable slots 34 (`WndNotification`), 49 (`OnProcessFrame`). Map window full vtable struct copied/replaced (`MQ2Map.cpp:221-254`).

**Struct layouts/members.** Key offsets: `SPAWNINFO` `Name 0x0A4`, `SpawnID 0x148`, `PetID 0x2B4`, `HPMax 0x2DC`, `HPCur 0x2E4`, `Class/Gender/Race/Level/StandState/Animation/InNonPCRaceIllusion` accessed throughout. `CXWnd` fields include vtable/first-child/sibling, `WindowText 0x1A8`, `dShow 0x196`, `SidlText 0x1DC`. `CharSelect` parsing uses `pData+243` for class bitmask, `pData[0]`/`pData[6]` for class override (`eqgame.cpp:692-700`). Shroud identity block offsets 0x00,0x04,0x08,0x0C,0x10,0x11,0x12 and `endOffset` at buf+4 (`eqgame.cpp:1017-1037` matches server).

**Rebasing.** Uniform `X-0x400000+baseAddress` except `VFTABLE_CGaugeWnd=0x9E87A8` (no rebase). `FUNCTION_AT_ADDRESS` uses absolute preferred VAs in naked mov.

**Version check.** `eqgame.cpp:2101-2110`: compares `__ExpectedVersionDate/Time` (`eqgame.h:19-23`) against runtime `__ActualVersionDate/Time`; aborts with `MessageBox("Incorrect client version")` if mismatch and `!__ClientOverride`. `__ClientOverride` is `0` in this build.

## 6. ACTIVE RUNTIME SUBSYSTEM MAP

| Subsystem | Init | Files | State/globals | Hooks | Packets | UI | EQ deps | Server deps | Trigger | User-visible |
|---|---|---|---|---|---|---|---|---|---|---|
| **Multiclass/CAuth** | `eqgame.cpp:2067` (WhoMulticlass init) + packet capture | `eqgame.cpp`, `who_multiclass.cpp` | `g_cauth_bitmask` | `HandleWorldMessage`, `UdpSend`, `WorldRoutePacket`, `SendMessage`, `GetClassDesc` | `0x1338`→store mask, `0x7777` sent from Heartbeat | none direct | spawnID, EQ connection ptr | `statClassesBitmask` (key1), auth key | `OP_ServerAuthStats` + `Heartbeat()` | Auth gate; enables multiclass consumers |
| **WhoMulticlass** | `:2067` | `who_multiclass.cpp` | `s_instance` | `HandleWorldMessage` dispatch | `0x578c` (suppressed, reformatted) | chat output | none | class bitmask in `Class_` | inbound `0x578c` | `/who` shows `CLASS1/CLASS2` |
| **MultiPet** | `:2065` | `multi_pet.cpp`, `multi_pet.h` | `g_multiPet`, `m_spawnMap`, `m_petList` | `HandleWorldMessage` dispatch | `0x1341` (may suppress) | none (logic only) | spawn table | pet list (spawn_id,class_id) | inbound `0x1341` | multiple pet membership/state |
| **PetWindow** | `:2066` | `pet_window.cpp`, `pet_window.h` | `g_petWindow` | SIDL `WndNotification` detour, Pulse | none (consumes MultiPet) | Pet window gauges (XML-dependent) | window pointers | via MultiPet | pulse + UI events | secondary pet gauges/windows |
| **Waypoints** | `InitializeMQ2Windows()` calls `WaypointsWnd::Initialize()` (`MQ2Windows.cpp:212`) | `waypoint_window.cpp`, `waypoint_window.h`, `NMS_WaypointsWnd.xml` | `WaypointsWnd` singleton | XML injection path | `0x1402` suppressed; `0x1403` sent | custom SIDL window | window manager/XML | `0x1402/0x1403` | UI click + inbound list | waypoint UI + teleport requests |
| **Shroud** | fix applied unconditionally | `eqgame.cpp:1017-1037`, `:818-841`, `:353-386` | `g_shroudPoseFix`, `g_shroudHotbarFixesLeft`, `g_shroudHotbarNextTick` | `HandleWorldMessage` | `0x6562` (processed + pose/hotbar fix) | hotbar windows | actor state | 20472-byte profile + identity block | inbound `0x6562` | pose stability, hotbars restored |
| **Disc bands** | table loaded at init; maintained in pulse | `nms_disc_bands.h`, `Hooks.cpp:296-302`, `MQ2Pulse.cpp:390-393` | `g_discTimers` map | 3 disc detours (Get/Set/doCombatAbility) | `0x6989` pass-through | none | spell recast arrays | server banded IDs (>=20) | pulse + ability use | correct disc recast separation |
| **FTM/render** | lazy init on first `ProcessGameEvents` | `FloatingTextManager.*`, `MQ2FloatingText.cpp`, `RenderHooks.h` | `g_pFtm`, `g_pDevice` | D3D9 BeginScene/EndScene/Reset | none | overlay text | D3D9 device | none | render frame | floating combat text |
| **Char-select multiclass** | packet handlers | `eqgame.cpp:661-700`, `:1055-1071`, `:1142-1164` | `g_CharSelectInfo` | world/auth route handlers | `0x1340`, `0x4513/0x00d2` | char select UI | memory patch of select struct | bitmask in repurposed deity field | char-select packets | multiclass class display in select |

## 7. MULTICLASS CLIENT ARCHITECTURE (data flow)

**server authoritative multiclass state → packets → client interception → client state → each consumer**

1. Server computes class bitmask: `GetClassesBits()` (mask over owned classes). Stored in DB bucket `GestaltClasses` (character-scoped). Sent via:
- `OP_ServerAuthStats` stat `eStatClassesBitmask` (key 1) in `zone/inventory.cpp:3888-3898` (12-byte `StatEntry_Struct`)
- `WhoAll` uses `GetClassesBits()` when multiclassing enabled (`zone/entity.cpp:5006-5008`, `world/clientlist.cpp:834-851`, `:876`) placing bitmask into `Class_` field (`eq_packet_structs.h:3965`)
- Character select: server writes `pp.classes` into `cse->Deity` (`world/worlddb.cpp:175`) and sets `cse->ShroudClass = cse->Class` (`:187`)

2. Packets reach client:
- `OP_ServerAuthStats` (0x1338): `HandleWorldMessage_Detour` reads key==1 at offset stride 12, sets `g_cauth_bitmask = (uint32_t)val` (`eqgame.cpp:1041-1053`)
- `OP_WhoAllResponse` (0x578c): dispatched to `WhoMulticlass::OnIncomingMessage` (`:1078-1081`), which decodes bitmask (`who_multiclass.cpp:88-107`) and suppresses original (`:234`)
- `OP_SendCharInfo` (0x4513/0x00d2): `ProcessNMSCharSelect` reads `pData+243` as class bitmask, stores in `g_CharSelectInfo[charIndex].Classes`, overwrites `pData[0]` and `pData[6]` with `200+index` (`eqgame.cpp:661-700`)

3. Client state: `uint32_t g_cauth_bitmask` (`eqgame.cpp:33`) is canonical live mask (post-auth). `g_CharSelectInfo` holds per-slot mask/original class.

4. CAuth handshake: `FireCAuthHandshake()` (`:1214-1250`) called from `Heartbeat()` (`MQ2Pulse.cpp:351`) computes `hashValue = (uint64_t)g_cauth_bitmask * (uint64_t)spawnID`, XORs 8 bytes with `private_key 352236586` (`eqgame.cpp:1228`, matches `ruletypes.h:1273`), sends `0x7777` (262 bytes). Server verifies `decrypted == GetClassesBits()*GetID()` (`zone/client_packet.cpp:5181`).

5. Consumers (by file/lines): spell level min (`Hooks.cpp:1014-1046`), merchant usable classes (`:1108-1132`), Bard cast non-bard (`:1151-1210`), skill availability (`:1215-1258`), tracking/Ranger spoof gates (`:1267-1367`), disc band mapping uses banded IDs (`:1311-1338`), class labels/abbr (`MQ2Labels.cpp:14-909`, `:1664-1773`), taskbar title (`MQ2Pulse.cpp:563-618`), sneak/cast spoof (`:422-519`), chat suppression (`MQ2ChatHook.cpp:48-55`), item display scroll levels (`MQ2ItemDisplay.cpp:229-263`), merchant/bazaar columns (`Hooks.cpp:602-715`).

**Bard sentinel:** not assumed universally; consumers test bitmask membership (e.g. `HasSkill`/tracking gates test bits). The server-side `/who` filtering explicitly uses `HasClass()`/bit tests (`world/clientlist.cpp:553-558`, `zone/entity.cpp:4885-4890`).

## 8. NETWORK / OPCODE CONTRACT CATALOG

Canonical contracts (client intercepts first where noted):

| Opcode | Symbol | Dir | Sender | Receiver | Client impl | Server impl | Payload (key fields) | Meaning | Consumer | Stock EQ aware? |
|---|---|---|---|---|---|---|---|---|---|---|
| `0x1338` | `OP_ServerAuthStats` | S→C | zone | client | `eqgame.cpp:1041-1053` | `zone/inventory.cpp:3888-3898` | `uint32 count`, then `count × {uint32 key, uint64 val}` (pack(1), stride 12) | Bulk stats; `key==1` = `eStatClassesBitmask` | Sets `g_cauth_bitmask` | no (custom) |
| `0x1340` | (char-select multiclass) | S→C | world/zone | client | `eqgame.cpp:1055-1071`, `:1142-1157` | — (not sent in live path) | `MulticlassCharSelect_Struct` | Legacy/select path; client handlers exist but server path unused | fills `g_CharSelectInfo[].Classes`; suppressed | no |
| `0x1341` | `OP_PetList` | S→C | zone | client | `multi_pet.cpp:282-294` | `zone/pets.cpp:1140-1177` | `uint32 count`, `count×{uint32 spawn_id,uint32 class_id}` | Pet membership (authoritative) | `MultiPet::OnIncomingMessage`; may suppress | no (custom) |
| `0x1402` | `OP_WaypointList` | S→C | zone | client | `eqgame.cpp:1105-1108` | `zone/nms_waypoints.cpp:191-248` | `WaypointList_Struct` (5 bools, uint32 count, entries) | Waypoint list for window | `WaypointsWnd`; packet suppressed | no |
| `0x1403` | `OP_WaypointRequest` | C→S | client | zone | `waypoint_window.cpp:250,263,274,284` | `zone/client_packet.cpp:17326-17352` | `WaypointRequest_Struct` | Request teleport | Server validates size, calls `TransportToWaypoint` | no |
| `0x7777` | `OP_CAuth` | C→S | client | zone | `eqgame.cpp:1241-1249` | `zone/client_packet.cpp:5153-5190` | 262 bytes: `uint16 opcode`, `char authHash[256]`, `uint32 unk` (0) | Response: `XOR(hash(g_cauth_bitmask*spawnID), key)` | Server verifies; echoes if authorized | no (custom) |
| `0x578c` | `OP_WhoAllResponse` | S→C | world | client | `who_multiclass.cpp:229-238`, `:244-353` | `zone/entity.cpp:4930-5030`, `world/clientlist.cpp:801-894` | WhoAll header (0x40) + per-entry (formatMsgId,padding1,padding2,name,...,classValue bitmask,...) | `/who` list; classValue is bitmask when multiclass | Reformatted to chat; original suppressed | stock opcode number, **payload semantics extended** (bitmask in Class_) |
| `0x6562` | `OP_Shroud` | S→C | zone | client | `eqgame.cpp:1017-1037` | `common/patches/rof2.cpp:5194-5291` | `uint32 spawn_id`, `uint16 end_offset`, spawn bytes, `kShroudProfileBlockSize=20472` profile block; identity at end_offset+0x00(shrouded),+0x08(gender),+0x0C(race),+0x10(class),+0x11,0x12(level) | Shroud transform state | Trampoline + sets `g_shroudPoseFix`, schedules hotbar restores | native RoF2 opcode (uses native number) |
| `0x6989` | `OP_DisciplineTimer` | S→C | zone | client | `eqgame.cpp:1093-1096` (pass-through) | `zone/effects.cpp:1372-1380`, `zone/client.cpp:7015-7019` | Stock disc timer | Timer updates; **must pass untouched** (client uses band map) | Consumed via detoured consumer funcs | stock |
| `0x4513/0x00d2` | `OP_SendCharInfo` | S→C | world | client | `eqgame.cpp:661-700`, `:1074-1076`, `:1160-1164` | `world/worlddb.cpp:175,187` | Char select struct; bitmask stored in `cse->Deity` (repurposed) | Multiclass char-select data | `ProcessNMSCharSelect` patches in-memory | stock opcodes, field repurposed |

**Custom opcodes present in `patch_RoF2.conf` with no active client handler:** `OP_MulticlassCharSelect` (`0x1340`) is defined but server never emits (client handlers exist but unreachable in live path). `OP_CustomDiscTimer` (`0x1400`) defined but no encoder/sender/handler on server or active client path.

## 9. PACKET INTERCEPTION PIPELINE

**Inbound (world/zone):** EQ network delivers packets to zone/world message handlers. Client detours `HandleWorldMessage` (`0x4C3250`, zone) and `WorldRoutePacket` (`0x5629C0`, world/auth). Both read 2-byte opcode (zone path receives opcode separately; world path reads from buffer start, skips 2 bytes when casting sub-packets). Dispatch order in `HandleWorldMessage_Detour` (`eqgame.cpp:1012-1128`): 0x6562 → process via trampoline then apply fixes (sets flags, does not suppress original behavior except side effects); 0x1338 → extract mask (non-suppressing); 0x1340 → parse and **suppress** (return 1); 0x4513/0x00d2 → call `ProcessNMSCharSelect` (non-suppressing mutation); 0x578c → `WhoMulticlass::OnIncomingMessage` may return false → **suppress** original; 0x1341 → `MultiPet::OnIncomingMessage` may suppress; switch handles 0x1338/0x1339/0x1402/0x1341/0x6b6d (0x1402 **suppresses**, 0x1339 processes then **suppresses** after trampoline call in that branch pattern). Packets not matching custom cases fall through to trampoline.

**Outbound (client→server):** UI/input produce packets via EQ send paths. `SendMessage_Detour` (`eqgame.cpp:1270-1364`) intercepts: checks `g_blockOutgoingPackets` (kill switch), offers to `WaypointsWnd::OnOutgoingPacket` (may suppress), rewrites `OP_AAAction` (0x424e/0x01e9) action 3→4 when Train-All checked, handles checksum scramble on 0xf13/0x578f. `UdpSend_Detour` (`:1203-1210`) is the low-level UDP send hook used to capture `g_zone_udp_con` (never suppresses normal sends). `SendEQMessage` (`:2360-2373`) is fork helper that prepends 2-byte opcode and sends via trampoline.

**Consumption model:** some packets are **fully consumed** (suppressed, reformatted) — `/who` 0x578c, waypoint list 0x1402, char-select 0x1340 in that path. Others are **monitored and mutated** (0x6562, 0x4513/0x00d2 char-select struct patching) then forwarded. Auth stats 0x1338 are read-only side effect.

## 10. UI ARCHITECTURE

**XML injection pipeline:** `CXMLSOMDocumentBase::XMLRead` detoured (`MQ2Windows.cpp:238`) rewrites requests for `EQUI.xml` to `MQUI.xml`. `GenerateMQUI()` copies active skin's `EQUI.xml` to `MQUI.xml` and injects `<Include>` lines at the **first `</Composite>`** tag (`:361-409` default, `:413-465` active skin). Duplicate-include guard: scans every `<Include>` on each line, trims and lowercases names, skips already-seen (`:325-359`). If active skin missing, falls back to `default` (`:428-439`). `AddXMLFile()` dedupes by filename and resolves active skin (`:496-517`); called only by `waypoint_window.cpp:78` with `"NMS_WaypointsWnd.xml"`.

**Custom windows:** `WaypointsWnd` derives `CCustomWnd("WaypointsWnd")` (`waypoint_window.cpp:39`); initialized in `InitializeMQ2Windows()` (`MQ2Windows.cpp:212`), shutdown via `NMS_DestroyCustomWindows()` (`MQ2CleanUI.cpp:53-60`) called from `CleanGameUI`/`ReloadUI` detours. `PetWindow` is a `CSidlScreenWnd` subclass (`pet_window.h:18`), initialized at `eqgame.cpp:2066`, shutdown `:2078`. Window notification vtable hooks applied where needed.

**Modified stock XML:** `ClientFiles\uifiles\default\EQUI_Inventory.xml` modified (Triune renames `IW_EOM*` → `IW_TriuneOfFate*`, lines ~3342-3367, 4051-4053). Skin variants differ (gearcore/shinsparxx carry different BazaarSearchWnd/MerchantWnd; Blue carries subset). `NMS_MapFilterWnd.xml` exists but is **never loaded** (§8e in original; confirmed not passed to `AddXMLFile()`).

**UI feature→impl→XML→server:** Waypoints UI → `waypoint_window.cpp` → `NMS_WaypointsWnd.xml` (default only) → `0x1402/0x1403`. Pet window → `pet_window.cpp` → requires `Pet2HPGauge`/`Pet3HPGauge` in `EQUI_PetInfoWindow.xml` (default) → driven by `MultiPet` (`0x1341`). Multiclass who renders via chat (no XML). Inventory Triune labels → `MQ2Labels.cpp:1767-1773` (EQType 338) → `EQUI_Inventory.xml`.

## 11. RENDERING AND GAME LOOP DEPENDENCIES

**D3D9 hooks:** `InstallD3D9Hooks()` (`Hooks.cpp:1857-2265`) creates NULLREF device to read vtable, hooks BeginScene (0x29), EndScene (0x2A), installs Reset later. This path is **lazy** (first `ProcessGameEvents`). `EndScene_Detour` (`RenderHooks.h:85-128`) calls `NMS_DrainCXStrAccess()` every frame (`:89`), sets `g_pDevice` on first cooperative test, initializes `g_pFtm` and hooks Reset (`:97-106`), renders FTM inside D3DSBT_ALL state block (`:117-124`).

**Pulse:** `Detour_ProcessGameEvents` → `MQ2Pulse.cpp:798-811`. `Heartbeat()` (`:305-800`) runs every frame: maintains disc bands (`:390-393`), runs MultiPet/PetWindow/Waypoints OnPulse (`:395-400`), alt-currency UI refresh (`:402-420`), sneak/cast spoof (`:422-519`), GCD clear (`:521-533`), scribe auto-open/close (`:536-558`), taskbar title update (5s throttle, `:563-618`), `NMS_DrainCXStrAccess()` (`:731-790`), fires `FireCAuthHandshake()` (`:351`), applies shroud pose/follow-up hotbar restores (`:353-386`), calls `MQ2ProcessPendingSpawns`/map state updates (`:830-836`).

**Timing dependencies:** many gameplay features depend on pulse (spoofs, disc maintenance, CAuth). Rendering path gates FTM initialization and Reset hook; the large set of gameplay detours (§4F) is installed inside `InstallD3D9Hooks()` (the function name is a misnomer) — so those ~24 detours exist only if D3D9 device probe succeeds. `NMS_DrainCXStrAccess()` runs every EndScene and also at pulse start (`:800`).

## 12. MQ2 HERITAGE VERSUS LIVE NMS CODE

Classification:

**A. Active NMS-specific:** `who_multiclass.*`, `multi_pet.*`, `pet_window.*`, `waypoint_window.*`, `nms_disc_bands.h`, `core_*.h`, `_options.h`, NMS-specific blocks in `eqgame.cpp` (shroud/CAuth/char-select), `MQ2Pulse.cpp` NMS blocks (drain, disc bands, spoofs, CAuth), `MQ2Windows.cpp` duplicate-include guard + waypoint init, `MQ2Labels.cpp` NMS label callbacks (EQType 338, class tables), `RenderHooks.h` NMS coexistence note.

**B. Active reused MQ2 infrastructure:** detour API (`MQ2DetourAPI.cpp`), command API/base (`MQ2CommandAPI.cpp`, only `/who,/fov,/camera` registered live), window/XML framework (`MQ2Windows.cpp`), pulse/heartbeat (`MQ2Pulse.cpp` core), spawn utilities (`MQ2Spawns.cpp`), map window hooks (`MQ2Map.cpp`), item display/labels framework (`MQ2ItemDisplay.cpp`, `MQ2Labels.cpp` framework), chat hooks (`MQ2ChatHook.cpp`), utilities (`MQ2Utilities.cpp` subset used), globals/offsets (`MQ2Globals.cpp`), `EQClasses.cpp` bindings.

**C. Compiled but unreachable/dead:** `MQ2Data*.cpp`, `MQ2DataTypes.cpp`, `MQ2DataVars.cpp`, `MQ2ParseAPI.cpp`, `MQ2MacroCommands.cpp`, `MQ2Mouse.cpp`, `MQ2MapCommands.cpp`, `EQUtils.cpp`, `MQ2PluginHandler.cpp` (minimal), `EQFunctionDetours.cpp`. Reason: `InitializeParser()` never called (only from dead `MQ2Initialize()`), so data-type tables never populated; command registration for MQ2 macro/advanced commands never runs. `ExitHooks()` unreachable; `UdpRoutePacket_Detour` not installed.

**D. Present but not compiled:** `MQ2Main.cpp`, `MQ2AdvDps.cpp`, `MQ2MMODPS.cpp`, `MQ2KeyBinds.cpp`, `MQ2Benchmarks.cpp`, `MQ2DInput.cpp`, `MQ2FloatingDamage.cpp`, `dllmain.cpp`, `main.cpp`, `EQNetworkDetours.cpp`, `keyboard.cpp`, `proxy_idi/idid.cpp`, `PE.cpp`, `d3d_example.cpp`, `ISXEQ/*`, `msvc/*`, `str_com/*`.

**E. Support:** Detours, DX9, `tweeny/*` (header-only, used by FTM), `AddressLookupTable.h`, proxy COM classes.

## 13. GLOBAL STATE MAP

| Global | Type | Owner | Readers/Writers | Purpose |
|---|---|---|---|---|
| `baseAddress` | `DWORD` | `MQ2Globals.cpp:30` (static) | many | Module base |
| `eqGraphicsAddress`, `eqMainAddress` | `uintptr_t` | `MQ2Globals.cpp:31-32` | offset resolver | Module handles |
| `g_cauth_bitmask` | `uint32_t` | `eqgame.cpp:33` | `:1048` writer; ~40 readers | Canonical multiclass mask |
| `g_zone_udp_con` | `DWORD*` | `eqgame.cpp:26` | `UdpSend_Detour` writer (`:1208`); `FireCAuthHandshake` reader (`:1218`) | Zone UDP connection |
| `g_CharSelectInfo[250]`, `g_CharSelectCount` | arrays/count | `eqgame.cpp:657-658` | `ProcessNMSCharSelect` writer; detours readers | Char-select multiclass cache |
| `g_shroudPoseFix`, `g_shroudHotbarFixesLeft`, `g_shroudHotbarNextTick` | bool/int/DWORD | `eqgame.cpp:812-814`, `:990-991` | `HandleWorldMessage_Detour` writer; `Heartbeat()` readers | Shroud flow control |
| `g_blockOutgoingPackets`, `g_forceSkillAvailable` | bool | `eqgame.cpp:710-711` | multiple writers/readers (scribe/AA paths) | Packet/skill gating |
| `g_pDevice` | `IDirect3DDevice9*` | `RenderHooks.h:29` | EndScene/Reset | D3D device |
| `g_pFtm` | `FloatingTextManager*` | `FloatingTextManager.cpp:9` | render/init | FTM singleton |
| `g_multiPet`, `g_petWindow`, `g_whoMulticlass` | singletons | `eqgame.cpp:28-30` | init/shutdown/pulse/dispatch | Feature mods |
| `g_discTimers` | `std::map<int,int>` | `Hooks.cpp:249` | disc detours + maintenance | Banded disc timers |
| `ProxyAddressLookupTable` | template instance | `eqgame.cpp:2233` | COM proxy | DI vtable mapping |
| `gDetourCS` | `CRITICAL_SECTION` | `eqgame.cpp:1597` | detour API | Synchronization |

## 14. THREADING / EXECUTION CONTEXT

- `DllMain` runs in loader lock context (Windows DLL load). No `LoadLibrary` of non-system DLLs except system `dinput8.dll`; no thread creation in compiled code (the `CreateThread` at `MQ2Main.cpp:61` is uncompiled).
- Hook installation (`InitHooks`) runs on the thread that loaded the DLL (EQ startup thread), before game enters main loop.
- Detours execute in the calling thread context: network callbacks on EQ network/zone threads (zone message handlers), `ProcessGameEvents` on EQ main/game thread, D3D9 `BeginScene/EndScene/Reset` on render thread.
- `Heartbeat()`/pulse runs on main thread. `NMS_DrainCXStrAccess()` called from EndScene (render thread) and pulse (main thread) — reads/writes `CRITICAL_SECTION` state but does not own it unless current thread already owns; logic checks `OwningThread == GetCurrentThreadId()` before `LeaveCriticalSection` loop (`MQ2Pulse.cpp:770-786`).
- `gDetourCS` protects detour state (`MQ2DetourAPI.cpp:346-366`, `:950`).
- No worker threads created by compiled code. State is effectively single-threaded per call site with the noted CS read.

## 15. FILE-BY-FILE ROLE MAP (key files)

| File | Role | Notes |
|---|---|---|
| `eqgame.cpp` | Bootstrap, DI proxy exports, master detours, packet dispatcher, shroud/CAuth/char-select | 84847B; largest behavioral delta vs Tunaria |
| `Hooks.cpp` | Gameplay detour table (~24), disc bands, multiclass consumers, D3D9 init | 77564B |
| `MQ2Pulse.cpp` | Heartbeat, shroud follow-ups, CAuth fire, drain, spoofs, vtable hooks | 34332B; significant delta |
| `MQ2Windows.cpp` | XML injection + duplicate-include guard + window registry | 41585B; delta |
| `MQ2Globals.cpp` | Offsets/base resolution | delta (no `LoadLibrary` from DllMain path) |
| `MQ2Utilities.cpp` | Utilities; `MQ2DataError` implementation | small delta |
| `MQ2Commands.cpp` | Command table; `/filter mq2data` alias | tiny delta |
| `MQ2Labels.cpp` | Label callbacks incl. EQType 338 Triune | size-equal hash-diff (content) |
| `MQ2CleanUI.cpp` | Clean/reload UI; calls `NMS_DestroyCustomWindows()` and drain | delta |
| `who_multiclass.cpp` | `/who` packet reformat (0x578c) | NMS module |
| `multi_pet.cpp` | Pet list logic (0x1341) | NMS module |
| `pet_window.cpp` | Pet UI window | NMS module |
| `waypoint_window.cpp` | Waypoint UI + requests (0x1403) | NMS module; delta (Taelosia category) |
| `RenderHooks.h` | D3D9 hooks + `NMS_DrainCXStrAccess()` | delta |
| `core_eqg_load.h` | EQG override with null-check | delta |
| `_options.h` | Feature toggles (horse true) | delta (−1B) |
| `FloatingTextManager.*`, `MQ2FloatingText.cpp` | FTM render system | active |
| `AddressLookupTable.h`, `dinput8.h`, proxy COM classes | DI proxy infrastructure | active |
| `nms_disc_bands.h` | Band table | active |
| `core_*.h` | InitOptions injectors | active |
| `EQUI_Inventory.xml` | UI tweak (Triune labels) | modified |
| Current-only: `build_eqgame_dll*.log`, `ClientFiles/Resources/GlobalLoad*.txt` | Local artifacts/generated data | not runtime deps of DLL |
| Tunaria-only: `Release/` build artifacts, `ClientFiles/dinput8.dll`, `desktop.ini` | Build outputs/metadata | excluded |

## 16. SERVER DEPENDENCY MAP

| Client dependency | Client evidence | Server evidence | Contract strength |
|---|---|---|---|
| `statClassesBitmask` (key 1) → `g_cauth_bitmask` | `eqgame.cpp:1041-1053` | `zone/inventory.cpp:3432-3433`, `:3888-3898`; `eq_packet_structs.h:1415` | **strong** |
| CAuth `0x7777`: `hash = mask*spawnID XOR key(352236586)` | `eqgame.cpp:1214-1250`, key `:1228` | `zone/client_packet.cpp:5153-5190`; key `ruletypes.h:1273`; verify uses `GetClassesBits()*GetID()` | **strong** |
| `/who` bitmask in `Class_` (0x578c) | `who_multiclass.cpp:88-107`, `:229-353` | `zone/entity.cpp:5006-5008`, `world/clientlist.cpp:834-851`, `:876`, `eq_packet_structs.h:3965` | **strong** (note parse alignment §20) |
| Pet list `0x1341` (spawn_id,class_id) | `multi_pet.cpp:282-294` | `zone/pets.cpp:1126-1177`, `:1140-1177` | **strong** |
| Waypoints `0x1402/0x1403` + structs | `waypoint_window.cpp:4-26,78,250,263,274,284`; `eqgame.cpp:1105-1108` | `zone/nms_waypoints.cpp:191-248`; `zone/client_packet.cpp:17326-17352`; `eq_packet_structs.h:1580-1603`; 4 DB tables | **strong** |
| Shroud `0x6562` (20472 block, identity offsets) | `eqgame.cpp:1017-1037` | `common/patches/rof2.cpp:5006,5014-5026,5194-5291`; `zone/shroud.cpp` | **byte-exact** |
| Char-select bitmask in deity field | `eqgame.cpp:661-700`, `:694` | `world/worlddb.cpp:175,187` | **strong** (repurposed field) |
| Disc bands >=20 | `nms_disc_bands.h`, `Hooks.cpp:1311-1338`, `MQ2Pulse.cpp:390-393` | `spells_new_repository.h:124-143` (×20·(class+1)); `MAX_DISCIPLINE_TIMERS=399` | **strong** |
| `OP_DisciplineTimer` pass-through required | `eqgame.cpp:1093-1096` | server emits banded IDs | **strong** |

## 17. CLIENT DATA / FILE DEPENDENCIES

**Required at runtime (client files):** `ClientFiles/uifiles/default/NMS_WaypointsWnd.xml`, `EQUI_Inventory.xml`, `EQUI_PetInfoWindow.xml`, `EQUI_BazaarSearchWnd.xml`, `EQUI_CharacterListWnd.xml`, `EQUI_MerchantWnd.xml`. Skin variants differ; `NMS_MapFilterWnd.xml` never loaded. `ClientFiles/Resources/GlobalLoad.txt`, `GlobalLoad_chr.txt` are consumed by EQ client (model load lists), not by DLL. DB-derived exports `spells_us.txt`, `dbstr_us.txt`, `SkillCaps.txt`, `BaseData.txt` are **absent** from Resources in repo (deploy-time via `export-client-files.bat`) — features B1/B9/B10/B14 assume they exist in client folder.

**Required to build:** project files, headers/libs (Detours, DX9), source as per §2. No external data files needed to compile.

**Configs:** `_options.h` compile-time only. No INI read at runtime by compiled DLL ( `ParseINIFile` only called from dead path).

## 18. RUNTIME REACHABILITY GRAPH

```
eqgame.exe loads dinput8.dll
  → DllMain(DLL_PROCESS_ATTACH) (eqgame.cpp:2241)
    → InitHooks() (1589)
      → InitOffsets/GetEQPath/gDetourCS (1595-1597)
      → if (isMQInjectsEnabled): InitializeMQ2Detours/DisplayHook/ChatHook/Commands/Windows/Pulse/Spawns/MapPlugin/ItemDisplay/Labels (1600-1610)
      → InitOptions() (1643) [applies core_*.h injectors]
      → unconditional detours: SendMessage, HandleWorldMessage, ApplyShroudFix, UdpSend, WorldRoutePacket, GetClassDesc (1666-1688)
      → option patches (1705-1980)
      → g_multiPet.Initialize(), g_petWindow.Initialize(), g_whoMulticlass.Initialize() (2065-2067)
    → resolve DI exports, forward (2273-2358)
  (attach complete)

Deferred/triggered:
  ProcessGameEvents first call → InitializeFloatingTextPlugin() (MQ2Pulse.cpp:802) → InstallD3D9Hooks() (Hooks.cpp:1857)
    → installs D3D9 BeginScene/EndScene/Reset + ~24 gameplay detours
  First ingame pulse → vtable hooks (CombatAbility/Tracking) (MQ2Pulse.cpp:311,317-318)
  OP_ServerAuthStats (0x1338) → set g_cauth_bitmask (eqgame.cpp:1048)
  UdpSend sees con + mask → set g_zone_udp_con (eqgame.cpp:1208)
  Heartbeat() → FireCAuthHandshake() when ready (MQ2Pulse.cpp:351)
  Inbound 0x578c → WhoMulticlass reformats+suppress (who_multiclass.cpp:234)
  Inbound 0x1341 → MultiPet may suppress (multi_pet.cpp:294)
  Inbound 0x1402 → suppressed (eqgame.cpp:1107)
  Inbound 0x6562 → fixes applied (eqgame.cpp:1031-1036)
  XML load → CXMLSOMDocumentBase::XMLRead rewrites EQUI→MQUI → GenerateMQUI injects NMS_WaypointsWnd.xml (MQ2Windows.cpp:169-465)

Unconditional branches: solid; conditional: flags in _options.h; packet-triggered: heavy; render-triggered: FTM init; pulse-triggered: heartbeat/state.

Dead/unreachable (not in graph): MQ2Initialize/MQ2Start, ExitHooks, UdpRoutePacket detour, InitializeParser/data subsystems.
```

## 19. CLIENT/SERVER END-TO-END FEATURE MATRIX

| Feature | Client entry | Client hooks/files | Opcode/packet | Server impl | UI dep | Memory/offset dep | Runtime trigger |
|---|---|---|---|---|---|---|---|
| **CAuth** | `FireCAuthHandshake()` (`eqgame.cpp:1214`) | `UdpSend_Detour` (`:1203`), `HandleWorldMessage` (`:1041`) | `0x1338` (in), `0x7777` (out) | `zone/client_packet.cpp:5153-5190`; `zone/inventory.cpp:3888-3898` | none | `g_cauth_bitmask`, `g_zone_udp_con`, spawnID | `Heartbeat()` when mask+conn+spawn ready |
| **Multiclass `/who`** | `WhoMulticlass::OnIncomingMessage` (`who_multiclass.cpp:229`) | `HandleWorldMessage` dispatch | `0x578c` (in, suppressed) | `zone/entity.cpp:4930-5030`, `world/clientlist.cpp:834-851,876` | chat | bitmask decode (`:88-107`), parse offsets (`:249-300`) | inbound 0x578c |
| **MultiPet** | `MultiPet::OnIncomingMessage` (`multi_pet.cpp:282`) | `HandleWorldMessage` dispatch; Pulse (`MQ2Pulse.cpp:397`) | `0x1341` (in, may suppress) | `zone/pets.cpp:1140-1177` | none | spawn map | inbound 0x1341 + pulse |
| **PetWindow** | `PetWindow::Initialize` (`pet_window.cpp:315`) | Pulse (`MQ2Pulse.cpp:398`), WndNotification detour | consumes MultiPet state | none (client-only UI) | `EQUI_PetInfoWindow.xml` (Pet2/3 HP gauges) | window offsets (`:30-43`), `VFTABLE_CGaugeWnd` (`:56` unrebased) | pulse + UI |
| **Waypoints** | `WaypointsWnd::Initialize` (`waypoint_window.cpp:77`) | XML injection (`MQ2Windows.cpp:212`), `SendMessage_Detour` (`eqgame.cpp:1285`), Pulse | `0x1402` (in,suppressed), `0x1403` (out) | `zone/nms_waypoints.cpp:191-248`, `zone/client_packet.cpp:17326-17352` | `NMS_WaypointsWnd.xml` | waypoint structs pack(1) | UI click + inbound list |
| **Shroud** | `HandleWorldMessage` branch (`eqgame.cpp:1017`) | Pulse follow-ups (`MQ2Pulse.cpp:353-386`) | `0x6562` (in, processed) | `common/patches/rof2.cpp:5194-5291`, `zone/shroud.cpp` | none | 20472 block, identity offsets | inbound 0x6562 |
| **Disc bands** | `Heartbeat()` maintenance (`MQ2Pulse.cpp:390-393`) | 3 detours (`Hooks.cpp:1316,1332,1345`) | `0x6989` (in, pass-through) | `spells_new_repository.h:124-143` | none | `g_discTimers`, band table | pulse + ability use |
| **Char-select multiclass** | `ProcessNMSCharSelect` (`eqgame.cpp:661`) | `HandleWorldMessage`, `WorldRoutePacket` | `0x4513/0x00d2`, `0x1340` | `world/worlddb.cpp:175,187` | char-select UI | `pData+243`, `pData[0]/[6]`, `g_CharSelectInfo` | select packets |
| **FTM** | `InitializeFloatingTextPlugin()` (`MQ2FloatingText.cpp:85`) | D3D9 BeginScene/EndScene/Reset | none | none | overlay | `g_pDevice`, `g_pFtm` | render frame (lazy) |

## 20. OBSERVED STRUCTURAL ANOMALIES (factual only)

1. **`VFTABLE_CGaugeWnd = 0x9E87A8` unrebased** (`pet_window.cpp:56`) — only address in that file not using `((X-0x400000)+baseAddress)`. **Unknown** whether this is intentional or a latent RoF2-build dependency.
2. **`who_multiclass.cpp:286-289` reads 3 uint32s (formatMsgId,padding1,padding2)** before name; server serializes 2 (`world/clientlist.cpp:858-861`: formatstring,pidstring). **Apparent layout mismatch**; runtime behavior not verified from source alone. **Unknown**.
3. **`InstallD3D9Hooks()` name misleading** — installs ~24 gameplay detours in addition to D3D9 (`Hooks.cpp:1857-2265`). **Factual naming mismatch**.
4. **Unbalanced `#pragma pack(push,1)`/`pop`**: `eqgame.cpp:555` push, `:615` push, `:649` pop (one pop for two pushes). Affects `CharSelectEquip` and `MulticlassCharSelect_Struct` packing scope. **Load-bearing** for wire layout; **unknown** if intentional.
5. **`g_CharSelectBitmasks` extern with no definition** (`Hooks.cpp:34`) — live code uses `g_CharSelectInfo` instead. **Orphaned declaration**.
6. **`ExitHooks()` never called**; `bInitalized` never set true (`eqgame.cpp:1577`, assignment at `:2064` commented). Module shutdown partial. **Factual lifecycle gap**.
7. **`MQ2Initialize()`/`MQ2Start()` duplicate init sequence but unreachable** — risk if ever invoked (double detour install). **Dormant code path**.
8. **`NMS_MapFilterWnd.xml` never loaded** (`AddXMLFile` never called). **Dead UI asset**.
9. **Skins not in sync** (gearcore/shinsparxx/Blue missing some NMS XML). **Deployment detail**.
10. **Resources missing four DB-derived client files**; `isSpellDataCRCEnabled=false`. **Deployment dependency**.
11. **`0x7EBE` caster mask literal** (`MQ2Labels.cpp:1570,1589,1607`) undocumented. **Unknown rationale**.
12. **`VFTABLE_CGaugeWnd` absolute VA** vs rebased convention. **ASLR/relocation sensitivity uncertain**.

## 21. UNKNOWN / UNPROVEN AREAS

- Runtime effect of the `/who` 3-vs-2 dword parse mismatch (§20 #2) — requires packet capture, not provable from static source.
- Whether `VFTABLE_CGaugeWnd` absolute VA is correct for this exact RoF2 build across all environments.
- Whether the nested `#pragma pack` in `eqgame.cpp` produces the intended wire layout for all compilers.
- Impact of `InstallD3D9Hooks()` gating ~24 gameplay detours if NULLREF device creation fails (no visible fallback).
- Whether `NMS_MapFilterWnd.xml` was intended to be loaded in some configuration.
- Complete set of skin-specific XML include paths under non-default skins.
- Whether `g_CharSelectBitmasks` was meant to replace `g_CharSelectInfo` historically.

## 22. FINAL ARCHITECTURE MODEL AND ASCII DIAGRAM

**Mental model (layers):**

1. **Bootstrap/proxy layer** — DI export forwarding (`dinput8.def`), COM vtable wrapping (`AddressLookupTable`, `IDirectInput*Hook`), `DllMain` attach.
2. **EQ memory binding layer** — preferred-base constants (`eqgame.h`), `FUNCTION_AT_ADDRESS` bindings (`EQClasses.cpp`), `InitOffsets()` rebasing, struct mirrors (`EQData.h`, `EQClasses.h`).
3. **Hook infrastructure** — Detours (`EzDetour`), table-driven `InstallDetour` registry, vtable patching, D3D9 device probe/install.
4. **MQ2-derived infrastructure** — window/XML injection, pulse/heartbeat, command framework, map/plugin scaffolding (subset active).
5. **NMS feature layer** — multiclass/auth, who/pets/waypoints/shroud, disc bands, UI customizations, spoofs.
6. **Client/server protocol layer** — opcode interception/serialization, custom contracts (`0x1338,0x1341,0x1402,0x1403,0x7777,0x578c,0x6562`), field repurposing (deity→bitmask).
7. **UI/render layer** — XML generation (`MQUI.xml`), custom SIDL windows, FTM (D3D9), labels.

**ASCII architecture diagram:**

```
EQCLIENT (eqgame.exe, RoF2)
│
▼
dinput8.dll (this)  ──DI exports──► system32\dinput8.dll (forwarded)
│
├─ DllMain
│  └─ InitHooks
│     ├─ offsets/base
│     ├─ MQ2 init block (gated)
│     ├─ unconditional detours (network/pulse/UI base)
│     ├─ patches (saylink/AA/shroud)
│     └─ NMS mods init (MultiPet/PetWindow/WhoMulticlass)
│
├─ Network layer
│  ├─ HandleWorldMessage (0x4C3250) ── dispatch: 0x6562,0x1338,0x1340,0x4513/0x00d2,0x578c,0x1341,0x1402,0x6b6d
│  ├─ WorldRoutePacket (0x5629C0)   ── login/auth path
│  ├─ UdpSend (0x8C51F0)            ── capture g_zone_udp_con
│  └─ SendMessage (0x8C4CE0)         ── outbound UI packets
│
├─ Game loop
│  └─ ProcessGameEvents detour (Heartbeat)
│     ├─ FireCAuthHandshake (0x7777)
│     ├─ disc bands, spoofs, shroud follow-ups
│     ├─ MultiPet/PetWindow/Waypoints OnPulse
│     └─ NMS_DrainCXStrAccess
│
├─ Render
│  └─ InstallD3D9Hooks (lazy)
│     ├─ BeginScene/EndScene/Reset (vtable)
│     ├─ ~24 gameplay detours
│     └─ FTM render (EndScene)
│
├─ UI/XML
│  ├─ CXMLSOMDocumentBase::XMLRead (EQUI→MQUI)
│  ├─ GenerateMQUI + duplicate-include guard
│  ├─ AddXMLFile → inject NMS_WaypointsWnd.xml
│  └─ WaypointsWnd/PetWindow SIDL windows
│
├─ NMS feature mods
│  ├─ WhoMulticlass (0x578c)
│  ├─ MultiPet (0x1341)
│  ├─ PetWindow
│  └─ Waypoints (0x1402/0x1403)
│
└─ State
   ├─ g_cauth_bitmask ← OP_ServerAuthStats (0x1338)
   ├─ g_zone_udp_con ← UdpSend
   ├─ g_CharSelectInfo ← OP_SendCharInfo (0x4513/0x00d2)
   └─ g_discTimers (banded disc timers)
```

**End of dossier.** No edits performed.
