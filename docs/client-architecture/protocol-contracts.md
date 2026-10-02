# NMS Client/Server Protocol Contracts

Analyzed repository: D:\NMS Source
Analyzed component: Release-NMS-Client (with server contract verification from Release-NMS-Server)
Analyzed commit: d851cfd8830ad80361d85af5f12800a5837aa3e8
Analysis date: 2026-10-02
Target client: RoF2 (Ring of Fire 2)
Pack/alignment: `#pragma pack(1)` on all wire structs; global `/Zp1` in client build

---

## CAuth Formula (Verified)

```
hashValue = ClassesBits * SpawnID
authHash  = XOR(hashValue bytes, private_key = 352236586)
packet    = 262-byte OP_CAuth (0x7777)
```

- `ClassesBits` = `GetClassesBits()` — the player's owned-class bitmask (uint32)
- `SpawnID` = player's spawn ID (uint32)
- `private_key` = `352236586` (source-visible constant in `eqgame.cpp:1228` and `ruletypes.h:1273`)
- Server verifies: `decrypted == GetClassesBits() * GetID()` (`zone/client_packet.cpp:5181`)

---

## Field Repurposing

### Character Select — Deity Field Carries Class Bitmask

- **Location:** `world/worlddb.cpp:175`
- **Behavior:** Server writes `pp.classes` (the multiclass bitmask) into `cse->Deity` (a field normally used for deity ID).
- **Client read:** `eqgame.cpp:694` reads `pData+243` as the class bitmask from the character-select struct.
- **Also:** Server sets `cse->ShroudClass = cse->Class` (`world/worlddb.cpp:187`).

### WhoAll — Class_ Field Carries Multiclass Bitmask

- **Location:** `zone/entity.cpp:5006-5008`, `world/clientlist.cpp:834-851,876`
- **Behavior:** When multiclassing is enabled, server places `GetClassesBits()` into the `Class_` field of each WhoAll entry (`eq_packet_structs.h:3965`).
- **Client read:** `who_multiclass.cpp:88-107` decodes the bitmask from `Class_`.
- **Note:** The `Class_` field normally holds a single class ID. When multiclassing is enabled, it carries a bitmask instead.

---

## OP_ServerAuthStats — 0x1338

| Attribute | Value |
|---|---|
| **Symbolic opcode** | `OP_ServerAuthStats` |
| **Numeric opcode** | `0x1338` |
| **Direction** | S→C |
| **Sender** | Zone |
| **Receiver** | Client |
| **Client source** | `eqgame.cpp:1041-1053` (`HandleWorldMessage_Detour`) |
| **Server source** | `zone/inventory.cpp:3888-3898` |
| **Payload layout** | `uint32 count`, then `count × {uint32 key, uint64 val}` (pack(1), stride 12) |
| **Field order/sizes** | count(4) + [key(4) + val(8)] × count |
| **Semantic meaning** | Bulk stat delivery. `key==1` = `eStatClassesBitmask` (multiclass bitmask). |
| **Stock/custom** | Custom (defined in `patch_RoF2.conf`) |
| **Client behavior** | Observes — reads `key==1` value into `g_cauth_bitmask`; does not suppress |
| **Runtime trigger** | Sent by zone on connection/stat update |
| **Affected state** | `g_cauth_bitmask` (client) |
| **Anomaly** | None |
| **Unknowns** | Full set of stat keys beyond key==1 not enumerated |

---

## OP_SkillTimers — 0x1339

| Attribute | Value |
|---|---|
| **Symbolic opcode** | `OP_SkillTimers` |
| **Numeric opcode** | `0x1339` |
| **Direction** | S→C |
| **Sender** | Zone |
| **Receiver** | Client |
| **Client source** | `eqgame.cpp:1093-1096` (pass-through in switch dispatch) |
| **Server source** | Defined in `patch_RoF2.conf`; no verified encoder found |
| **Payload layout** | Not verified — no serialization/deserialization observed |
| **Semantic meaning** | Skill timer updates (custom) |
| **Stock/custom** | Custom (defined in `patch_RoF2.conf`) |
| **Client behavior** | Pass-through (falls to trampoline) |
| **Runtime trigger** | Inbound packet with opcode 0x1339 |
| **Affected state** | None (pass-through) |
| **Anomaly** | No server encoder verified; may be defined-but-unused |
| **Unknowns** | Whether server ever emits this opcode; payload format unknown |

---

## OP_PetList — 0x1341

| Attribute | Value |
|---|---|
| **Symbolic opcode** | `OP_PetList` |
| **Numeric opcode** | `0x1341` |
| **Direction** | S→C |
| **Sender** | Zone |
| **Receiver** | Client |
| **Client source** | `multi_pet.cpp:282-294` (`MultiPet::OnIncomingMessage`) |
| **Server source** | `zone/pets.cpp:1140-1177` |
| **Payload layout** | `uint32 count`, then `count × {uint32 spawn_id, uint32 class_id}` |
| **Field order/sizes** | count(4) + [spawn_id(4) + class_id(4)] × count |
| **Semantic meaning** | Pet membership list (authoritative). Each entry maps a spawn ID to a class ID. |
| **Stock/custom** | Custom (defined in `patch_RoF2.conf`) |
| **Client behavior** | May suppress — `MultiPet::OnIncomingMessage` can return false to suppress original |
| **Runtime trigger** | Inbound 0x1341 |
| **Affected state** | `g_multiPet` internal pet list |
| **Anomaly** | None |
| **Unknowns** | None |

---

## OP_CustomDiscTimer — 0x1400

| Attribute | Value |
|---|---|
| **Symbolic opcode** | `OP_CustomDiscTimer` |
| **Numeric opcode** | `0x1400` |
| **Direction** | S→C (intended) |
| **Sender** | Zone (intended) |
| **Receiver** | Client (intended) |
| **Client source** | None — no handler found |
| **Server source** | None — no encoder found |
| **Payload layout** | Not verified |
| **Semantic meaning** | Custom discipline timer (intended) |
| **Stock/custom** | Custom (defined in `patch_RoF2.conf`) |
| **Client behavior** | N/A — no handler |
| **Runtime trigger** | None verified |
| **Affected state** | None |
| **Anomaly** | **Defined but unused** — present in `patch_RoF2.conf` with no encoder/sender/handler on server or active client path |
| **Unknowns** | Whether this was intended for future use or is vestigial |

---

## OP_CAuth — 0x7777

| Attribute | Value |
|---|---|
| **Symbolic opcode** | `OP_CAuth` |
| **Numeric opcode** | `0x7777` |
| **Direction** | C→S |
| **Sender** | Client |
| **Receiver** | Zone |
| **Client source** | `eqgame.cpp:1241-1249` (`FireCAuthHandshake`) |
| **Server source** | `zone/client_packet.cpp:5153-5190` |
| **Payload layout** | 262 bytes: `uint16 opcode`, `char authHash[256]`, `uint32 unk` (0) |
| **Field order/sizes** | opcode(2) + authHash(256) + unk(4) = 262 bytes |
| **Semantic meaning** | Client authentication response. Proves client knows the multiclass bitmask. |
| **Stock/custom** | Custom (defined in `patch_RoF2.conf`) |
| **Client behavior** | Sends (outbound) |
| **Runtime trigger** | `Heartbeat()` when `g_cauth_bitmask` is set and `g_zone_udp_con` is captured (`MQ2Pulse.cpp:351`) |
| **Affected state** | Server auth state; client `g_cauth_sent` flag |
| **Anomaly** | None |
| **Unknowns** | None |

---

## OP_WaypointList — 0x1402

| Attribute | Value |
|---|---|
| **Symbolic opcode** | `OP_WaypointList` |
| **Numeric opcode** | `0x1402` |
| **Direction** | S→C |
| **Sender** | Zone |
| **Receiver** | Client |
| **Client source** | `eqgame.cpp:1105-1108` |
| **Server source** | `zone/nms_waypoints.cpp:191-248` |
| **Payload layout** | `WaypointList_Struct` (5 bools, uint32 count, entries) |
| **Field order/sizes** | 5×bool(1) + count(4) + entries × entry_size |
| **Semantic meaning** | Waypoint list for the waypoint window UI |
| **Stock/custom** | Custom (defined in `patch_RoF2.conf`) |
| **Client behavior** | Suppresses — packet consumed by `WaypointsWnd`; original not passed to EQ |
| **Runtime trigger** | Inbound 0x1402 |
| **Affected state** | `WaypointsWnd` internal list |
| **Anomaly** | None |
| **Unknowns** | Exact entry struct layout not fully enumerated |

---

## OP_WaypointRequest — 0x1403

| Attribute | Value |
|---|---|
| **Symbolic opcode** | `OP_WaypointRequest` |
| **Numeric opcode** | `0x1403` |
| **Direction** | C→S |
| **Sender** | Client |
| **Receiver** | Zone |
| **Client source** | `waypoint_window.cpp:250,263,274,284` |
| **Server source** | `zone/client_packet.cpp:17326-17352` |
| **Payload layout** | `WaypointRequest_Struct` |
| **Field order/sizes** | Not fully enumerated |
| **Semantic meaning** | Request teleport to a waypoint |
| **Stock/custom** | Custom (defined in `patch_RoF2.conf`) |
| **Client behavior** | Sends (outbound) |
| **Runtime trigger** | UI click in waypoint window |
| **Affected state** | Server triggers `TransportToWaypoint` |
| **Anomaly** | None |
| **Unknowns** | Exact struct fields not fully enumerated |

---

## OP_MulticlassCharSelect — 0x1340

| Attribute | Value |
|---|---|
| **Symbolic opcode** | `OP_MulticlassCharSelect` |
| **Numeric opcode** | `0x1340` |
| **Direction** | S→C |
| **Sender** | World/zone (intended) |
| **Receiver** | Client |
| **Client source** | `eqgame.cpp:1055-1071`, `:1142-1157` |
| **Server source** | None — no encoder found |
| **Payload layout** | `MulticlassCharSelect_Struct` |
| **Semantic meaning** | Multiclass character-select data (legacy/select path) |
| **Stock/custom** | Custom (defined in `patch_RoF2.conf`) |
| **Client behavior** | Suppresses — fills `g_CharSelectInfo[].Classes` |
| **Runtime trigger** | Inbound 0x1340 |
| **Affected state** | `g_CharSelectInfo` |
| **Anomaly** | **Legacy/unemitted** — client handlers exist but server never emits this opcode in the live path. The active char-select multiclass data flows through `OP_SendCharInfo` (0x4513/0x00d2) instead. |
| **Unknowns** | Whether this opcode was used in an earlier protocol version |

---

## OP_CharacterSetRequest — 0x1404

| Attribute | Value |
|---|---|
| **Symbolic opcode** | `OP_CharacterSetRequest` |
| **Numeric opcode** | `0x1404` |
| **Direction** | C→S (intended) |
| **Sender** | Client (intended) |
| **Receiver** | Zone (intended) |
| **Client source** | None — no handler found |
| **Server source** | None — no handler found |
| **Payload layout** | Not verified |
| **Semantic meaning** | Character set request (intended) |
| **Stock/custom** | Custom (defined in `patch_RoF2.conf`) |
| **Client behavior** | N/A — no handler |
| **Runtime trigger** | None verified |
| **Affected state** | None |
| **Anomaly** | **Defined but inactive** — present in `patch_RoF2.conf` with no active handler on either side |
| **Unknowns** | Intended purpose unknown |

---

## OP_SuppressBuffNameInfo — 0x1409

| Attribute | Value |
|---|---|
| **Symbolic opcode** | `OP_SuppressBuffNameInfo` |
| **Numeric opcode** | `0x1409` |
| **Direction** | S→C (intended) |
| **Sender** | Zone (intended) |
| **Receiver** | Client (intended) |
| **Client source** | None — no handler found |
| **Server source** | None — no encoder found |
| **Payload layout** | Not verified |
| **Semantic meaning** | Suppress buff name info (intended) |
| **Stock/custom** | Custom (defined in `patch_RoF2.conf`) |
| **Client behavior** | N/A — no handler |
| **Runtime trigger** | None verified |
| **Affected state** | None |
| **Anomaly** | **Defined but inactive** — present in `patch_RoF2.conf` with no active handler on either side |
| **Unknowns** | Intended purpose unknown |

---

## OP_WhoAllResponse — 0x578c

| Attribute | Value |
|---|---|
| **Symbolic opcode** | `OP_WhoAllResponse` |
| **Numeric opcode** | `0x578c` |
| **Direction** | S→C |
| **Sender** | World |
| **Receiver** | Client |
| **Client source** | `who_multiclass.cpp:229-238`, `:244-353` (`WhoMulticlass::OnIncomingMessage`) |
| **Server source** | `zone/entity.cpp:4930-5030`, `world/clientlist.cpp:801-894` |
| **Payload layout** | WhoAll header (0x40 bytes) + per-entry: `formatMsgId(4)`, `padding1(4)`, `padding2(4)`, `name(...)`, ..., `classValue(4)` = bitmask when multiclass |
| **Field order/sizes** | header(0x40) + entries; per-entry has 3 uint32s before name (client reads formatMsgId, padding1, padding2) |
| **Semantic meaning** | `/who` list response. `classValue` field carries multiclass bitmask when multiclassing is enabled. |
| **Stock/custom** | Stock opcode number, **payload semantics extended** (bitmask in `Class_` field) |
| **Client behavior** | Suppresses — reformats to chat output; original packet not passed to EQ |
| **Runtime trigger** | Inbound 0x578c |
| **Affected state** | Chat display |
| **Anomaly** | **Parse discrepancy (inherited from Tunaria):** Client reads 3 uint32s (formatMsgId, padding1, padding2) before name (`who_multiclass.cpp:286-289`); server serializes 2 (formatstring, pidstring) (`world/clientlist.cpp:858-861`). This is a 4-byte desync. `who_multiclass.cpp` is bit-for-bit identical to Tunaria — this is inherited, not introduced by current Triptych changes. |
| **Unknowns** | Runtime effect of the parse mismatch requires packet capture to verify |

---

## OP_Shroud — 0x6562

| Attribute | Value |
|---|---|
| **Symbolic opcode** | `OP_Shroud` |
| **Numeric opcode** | `0x6562` |
| **Direction** | S→C |
| **Sender** | Zone |
| **Receiver** | Client |
| **Client source** | `eqgame.cpp:1017-1037` |
| **Server source** | `common/patches/rof2.cpp:5194-5291` |
| **Payload layout** | `uint32 spawn_id`, `uint16 end_offset`, spawn bytes, `kShroudProfileBlockSize=20472` profile block; identity at end_offset+0x00(shrouded), +0x08(gender), +0x0C(race), +0x10(class), +0x11, +0x12(level) |
| **Field order/sizes** | spawn_id(4) + end_offset(2) + spawn_data + profile_block(20472) |
| **Semantic meaning** | Shroud transform state — tells client about shrouded appearance |
| **Stock/custom** | Native RoF2 opcode (uses native number) |
| **Client behavior** | Observes + mutates — processes via trampoline, then applies pose fix and schedules hotbar restores |
| **Runtime trigger** | Inbound 0x6562 |
| **Affected state** | `g_shroudPoseFix`, `g_shroudHotbarFixesLeft`, `g_shroudHotbarNextTick` |
| **Anomaly** | None |
| **Unknowns** | None |

---

## OP_DisciplineTimer — 0x6989

| Attribute | Value |
|---|---|
| **Symbolic opcode** | `OP_DisciplineTimer` |
| **Numeric opcode** | `0x6989` |
| **Direction** | S→C |
| **Sender** | Zone |
| **Receiver** | Client |
| **Client source** | `eqgame.cpp:1093-1096` (pass-through) |
| **Server source** | `zone/effects.cpp:1372-1380`, `zone/client.cpp:7015-7019` |
| **Payload layout** | Stock discipline timer |
| **Semantic meaning** | Discipline timer updates. Server uses banded IDs (>=20) for multiclass. |
| **Stock/custom** | Stock opcode |
| **Client behavior** | Pass-through — **must pass untouched** (client uses band map to interpret) |
| **Runtime trigger** | Inbound 0x6989 |
| **Affected state** | `g_discTimers` (client band map) |
| **Anomaly** | None |
| **Unknowns** | None |

---

## OP_SendCharInfo — 0x4513 / 0x00d2

| Attribute | Value |
|---|---|
| **Symbolic opcode** | `OP_SendCharInfo` |
| **Numeric opcode** | `0x4513` / `0x00d2` |
| **Direction** | S→C |
| **Sender** | World |
| **Receiver** | Client |
| **Client source** | `eqgame.cpp:661-700`, `:1074-1076`, `:1160-1164` (`ProcessNMSCharSelect`) |
| **Server source** | `world/worlddb.cpp:175,187` |
| **Payload layout** | Character-select struct; bitmask stored in `cse->Deity` (repurposed field) |
| **Field order/sizes** | `pData+243` = class bitmask; `pData[0]` = class override; `pData[6]` = class override |
| **Semantic meaning** | Multiclass character-select data — delivers class bitmask and class info |
| **Stock/custom** | Stock opcodes, **field repurposed** (deity → bitmask) |
| **Client behavior** | Mutates — patches in-memory character-select struct; stores bitmask in `g_CharSelectInfo`; overwrites class fields with `200+index` to trigger `GetClassDesc_Detour` |
| **Runtime trigger** | Character-select packets |
| **Affected state** | `g_CharSelectInfo[250]`, `g_CharSelectCount` |
| **Anomaly** | None |
| **Unknowns** | None |

---

## Defined But No Active End-to-End Handler

The following opcodes are present in `patch_RoF2.conf` but have no proven live encoder/handler on either server or client:

| Opcode | Symbolic name | Status |
|---|---|---|
| `0x1339` | `OP_SkillTimers` | Defined in conf; no server encoder verified; client pass-through only |
| `0x1400` | `OP_CustomDiscTimer` | Defined in conf; no encoder/sender/handler on either side |
| `0x1404` | `OP_CharacterSetRequest` | Defined in conf; no handler on either side |
| `0x1409` | `OP_SuppressBuffNameInfo` | Defined in conf; no handler on either side |

These may be vestigial, intended for future use, or used in code paths not verified during this investigation.

---

**End of protocol contracts document.**
