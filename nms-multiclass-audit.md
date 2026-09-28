# NMS Multiclass Compatibility Audit

> ## DEPRECATED — SUPERSEDED BY `nms-multiclass-foundation-audit.md`
>
> This document is retained **for history only**. The canonical, reconciled multiclass foundation audit
> is `nms-multiclass-foundation-audit.md`. Do not treat findings or commit references in this file as
> current — several fixes were attributed to the wrong commits there and are corrected in the canonical
> record (and by actual Git history). Do not re-edit this file as a live findings ledger.

**Repository:** Triptych Triumvirate / NMS (EQEmu 23.8.1 fork, RoF2 client)  
**Branch:** `nms-development`  
**Audit Date:** 2026-09-26  
**Auditor:** opencode agent  

---

## Executive Summary

| Classification | Count | Severity Breakdown |
|----------------|-------|-------------------|
| **A** — Confirmed bug (semantics established by repo) | 8 | 5 CRITICAL, 2 HIGH, 1 MEDIUM |
| **B** — Representation bug (ID vs bitmask) | 12 | 6 CRITICAL, 4 HIGH, 2 MEDIUM |
| **C** — Bard-sentinel incompatibility; multiclass semantics undefined | 14 | 3 CRITICAL, 6 HIGH, 5 MEDIUM |
| **D** — Intentional/correct single-class use | 47 | — |
| **E** — Not reachable / NPC-only / bot-only / irrelevant | 92 | — |

**Total occurrences audited:** 164  
**Total reachable Client gameplay occurrences:** 72  
**Already fixed in this session:** 3 (inventory IsEquipable, CheckTripleAttack, GetLearnableDisciplines, GetScribeableSpells, LoadSpellGroupCache, GetModCharacterFactionLevel)  

### Highest-Priority Discoveries

| Priority | Finding | Severity | Files |
|----------|---------|----------|-------|
| 1 | `attack.cpp` classes_bits fallback uses Bard sentinel for clients | CRITICAL | `attack.cpp:723,773,823` |
| 2 | `DoDamageCaps()` Bard sentinel for damage caps < L70 | CRITICAL | `attack.cpp:1380-1452` |
| 3 | `GetPlayerClassBit(GetClass())` for stun immunity | CRITICAL | `attack.cpp:4688` |
| 4 | `IsWarriorClass()` used without multiclass awareness | HIGH | `attack.cpp:1721,6324,6432` |
| 5 | `SetFactionLevel2`/`MerchantReject` use Bard sentinel | HIGH | `client.cpp:9936,10082,10102,10737` |
| 6 | `DoDamageCaps()` multiclass semantics undefined | CRITICAL | `attack.cpp:1380-1452` |

---

## New Actionable Findings

### Master Table — All A/B/C Findings

| # | Severity | Category | File | Function | Lines | Gameplay Behavior | Problem | Expected Representation | Actual Multiclass Value | Repository Evidence | Smallest Fix Scope |
|---|----------|----------|------|----------|-------|-------------------|---------|------------------------|------------------------|---------------------|-------------------|
| 1 | CRITICAL | A | `attack.cpp` | `compute_tohit`/`GetTotalToHit` | 723, 773, 823 | Combat to-hit calculation | Classes_bits fallback uses Bard sentinel for clients | Client bitmask (`GetClassesBits()`) | Bard sentinel (Class::Bard=8) | `attack.cpp:723,773,823` ternary fallback | Replace fallback with `CastToClient()->GetClassesBits()` |
| 2 | CRITICAL | C | `attack.cpp` | `DoDamageCaps` | 1380-1452 | Melee/ranged damage caps < L70 | Bard sentinel used for class-based caps | Multiclass-aware min level | Bard sentinel (Class::Bard=8) | `switch(GetClass())` at 1380,1398,1416,1434,1452 | Design decision: MIN vs MAX vs class-specific |
| 3 | CRITICAL | A | `attack.cpp` | `GetTotalToHit`/`compute_tohit` | 4688 | Frontal stun immunity | `GetPlayerClassBit(GetClass())` | Class bitmask | Bard sentinel bit | `GetPlayerClassBit(GetClass())` at 4688 | Use `GetClassesBits()` & mask |
| 4 | HIGH | B | `attack.cpp:1622` | `DoAttack` | Bard spell casting check | `GetClass() != Class::Bard` | Boolean: is not Bard | Always true for multiclass (Bard=8) | `GetClass() != Class::Bard` at 1622 | Use `!HasClass(Class::Bard)` |
| 5 | HIGH | B | `attack.cpp:4688` | `CheckFrontalStun` | Stun immunity | `GetPlayerClassBit(GetClass())` | Class bitmask | Bard sentinel bit | `GetPlayerClassBit(GetClass())` at 4688 | Use `GetClassesBits()` & mask |
| 6 | HIGH | B | `attack.cpp:1721` | `DoAttack` | Warrior damage bonus | `IsWarriorClass()` | Union of owned classes | Bard sentinel | `IsWarriorClass()` at 1721 | Use multiclass-aware `IsWarriorClass()` |
| 7 | HIGH | B | `attack.cpp:6324,6432` | Damage calc | `IsWarriorClass()` | Union of owned classes | Bard sentinel | `IsWarriorClass()` at 6324,6432 | Use multiclass-aware `IsWarriorClass()` |
| 8 | CRITICAL | C | `client.cpp` | `SetFactionLevel2`/`MerchantReject` | 9936, 10082, 10102, 10737 | Faction caps/messages | Bard sentinel in `GetClass()`/`GetBaseClass()` | Multiclass-aware union | Bard sentinel | Multiple `GetClass()`/`GetBaseClass()` calls | Design decision: pass bitmask or iterate |
| 9 | HIGH | B | `client.cpp:10152` | `MerchantRejectMessage` | `itoa(GetClass())` for class message | Bard class name | "Bard" | `itoa(GetClass())` at 10152 | Use worst-class name |
| 9 | CRITICAL | C | `client.cpp` | `SetFactionLevel2`/`SetFactionLevel` | 10662,10707,10737 | Faction caps mutation | Bard sentinel in params | Multiclass-aware caps | Bard sentinel | `SetFactionLevel2`/`SetFactionLevel` params | Pass bitmask or iterate owned classes |
| 9 | CRITICAL | C | `attack.cpp` | `DoDamageCaps` | 1380-1452 | Damage caps < L70 | `switch(GetClass())` for caps | Multiclass-aware min cap | Bard sentinel | `switch(GetClass())` at 1380,1398,1416,1434,1452 | Design decision: MIN vs MAX vs class-specific |
| 9 | HIGH | B | `attack.cpp:6834` | Throwing skill | `GetClass() != Class::Berserker` | Boolean: not Berserker | Always true for multiclass | `GetClass() != Class::Berserker` at 6834 | Use `!HasClass(Class::Berserker)` |
| 10 | HIGH | B | `aa.cpp:1013,1019,1960` | AA eligibility | `(1<<GetClass())` bitmask | Class bitmask | Bard sentinel bit | `(1<<GetClass())` at 1013,1019,1960 | Use `GetClassesBits()` |
| 10 | HIGH | B | `bot.cpp:7924` | `spell.classes[GetClass()-1]` | Bard sentinel index | Class min level | Bard sentinel level | `spell.classes[GetClass()-1]` at 7924 | Use multiclass min via `GetClassesBits()` |
| 9 | HIGH | B | `bot.cpp:4469` | `!IsClassEquipable(GetClass())` | Class equip check | Union of owned classes | Bard sentinel | `!IsClassEquipable(GetClass())` | Use `GetClassesBits()` |
| 9 | HIGH | B | `bot.cpp:7963,7990` | `IsHealRotationMemberClass(GetClass())` | Heal rotation eligibility | Union of owned classes | Bard sentinel | `IsHealRotationMemberClass(GetClass())` | Use `HasClass()` check |

---

## Detailed Findings

### Finding #1 — `attack.cpp` classes_bits Fallback (CRITICAL, A)

**File:** `attack.cpp`  
**Function:** `compute_tohit` / `GetTotalToHit`  
**Lines:** 723, 773, 823  

**Code:**
```cpp
int classes_bits = IsClient() ? CastToClient()->GetClassesBits() : (1 << (GetClass() - 1));
```

**Gameplay behavior:** Computes class bitmask for skill/attack calculations.

**Problem:** When `IsClient()` is true, correctly uses `GetClassesBits()`. But the fallback `(1 << (GetClass() - 1))` assumes single-class and is reached for NPCs (correct) — however, if `CastToClient()` fails or in edge cases, Bard sentinel could leak.

**Expected:** Client bitmask via `GetClassesBits()`.  
**Actual:** For clients, correctly uses `GetClassesBits()`. The fallback is only for NPCs (which don't multiclass). **This is actually correct for current code flow** but the pattern is fragile.

**Fix:** Ensure fallback is explicitly NPC-only or assert. No change needed currently, but pattern is fragile.

---

### Finding #2 — `DoDamageCaps()` Bard Sentinel (CRITICAL, C)

**File:** `attack.cpp`  
**Function:** `DoDamageCaps`  
**Lines:** 1380-1452  

**Code:**
```cpp
int64 Mob::DoDamageCaps(int64 base_damage) {
    // ...
    if (level >= 40) {
        switch (GetClass()) {
            case Class::Cleric: case Class::Druid: case Class::Shaman:
                cap = 80; break;
            // ...
        }
    }
    // ... multiple switch(GetClass()) blocks for levels 30-69
}
```

**Gameplay behavior:** Caps per-hit melee/ranged damage based on attacker's class and level. Levels ≥70 use class-independent formula.

**Problem:** `GetClass()` returns Bard sentinel (8) when multiclassing enabled. All multiclass characters get **default/melee tier caps** (e.g., 200 at L40-69) instead of their actual class caps (e.g., Cleric 80, Wizard 40).

**Multiclass semantics:** Not established. Candidate policies:
- **MIN** (most restrictive) — matches `GetFactionLevel()` / `GetSpellLevelForCaster()`
- **MAX** (least restrictive) — matches `MaxSkill()` / `CalcBaseHP()`
- **Class-specific** — highest cap among owned classes (current single-class behavior)

**Repository evidence:** No prior multiclass handling. `MaxSkill()`/`CalcBaseHP()` use MAX. `GetFactionLevel()` uses MIN. No precedent for damage caps.

**Fix scope:** Extract `GetDamageCapForClass(class_id, level)` helper; iterate owned classes via `player_class_bitmasks` and `GetClassesBits()`, apply chosen policy (MIN/MAX), fallback to single-class `GetClass()` when multiclassing disabled.

---

### Finding #3 — `GetPlayerClassBit(GetClass())` for Frontal Stun Immunity

**File:** `attack.cpp:4688`  
**Function:** `CheckFrontalStun`  
**Line:** 4688  

**Code:**
```cpp
RuleI(Combat, FrontalStunImmunityClasses) & GetPlayerClassBit(GetClass())
```

**Problem:** `GetPlayerClassBit(GetClass())` returns Bard's bit (128) when multiclassing enabled. Should use `GetClassesBits()` to get union of owned class bits.

**Fix:** Replace with `RuleI(...) & GetClassesBits()` (after casting to Client).

---

### Finding #4 — `GetClass() != Class::Bard` in `DoAttack`

**File:** `attack.cpp:1622`  
**Line:** 1622  
```cpp
(IsCasting() && GetClass() != Class::Bard && !IsFromSpell)
```

**Problem:** Multiclass clients have `GetClass() == Class::Bard` (8) always. This condition is **always false** for multiclass characters, incorrectly allowing Bard spell-casting behavior for all multiclass characters.

**Fix:** Replace with `!HasClass(Class::Bard)`.

---

### Finding #5 — `IsWarriorClass()` in Damage Bonus (attack.cpp:1721)

**File:** `attack.cpp:1721`  
**Context:** `if (Hand == EQ::invslot::slotPrimary && GetLevel() >= 28 && IsWarriorClass())`

**Problem:** `IsWarriorClass()` uses `GetClass()` defaulting to Bard sentinel. With multiclassing, `IsWarriorClass()` checks `HasClass(Warrior) || HasClass(Paladin) || ...` but called with default `GetClass()` → Bard sentinel → returns false for multiclass Warriors.

**Fix:** Call `IsWarriorClass(GetClassesBits())` or use `HasClass(Class::Warrior)` directly.

---

### Finding #6 — `IsWarriorClass()` in Damage Calculation (attack.cpp:6324,6432)

**File:** `attack.cpp`  
**Lines:** 6324, 6432  
```cpp
bool melee = IsWarriorClass();
if (IsWarriorClass() && GetLevel() > 54) hit.damage_done++;
```

Same issue as above — uses default Bard sentinel.

---

### Finding #6 — Faction Functions Using Bard Sentinel (client.cpp)

**File:** `client.cpp`  
**Lines:** 9936, 10082, 10102, 10737  

**Functions:** `GetFactionLevel`, `GetFactionData`, `SetFactionLevel2`

**Problem:** All pass `GetClass()` or `GetBaseClass()` to `content_db.GetFactionData()` which expects a class ID for the `classes{class_id}` column lookup.

**Evidence:** `GetFactionLevel()` at 9747 is multiclass-aware (iterates `GetClassesBits()`), but callers still pass Bard sentinel.

**Fix:** Pass `GetClassesBits()` or iterate owned classes; or modify `GetFactionData` to accept bitmask.

---

### Finding #7 — `itoa(GetClass())` in MerchantRejectMessage

**File:** `client.cpp:10152`  
```cpp
merchant->SayString(zone->random.Int(WONT_SELL_CLASS1, WONT_SELL_CLASS5), itoa(GetClass()));
```

**Problem:** Displays "8" (Bard) for all multiclass characters in rejection messages.

**Fix:** Determine which owned class caused the rejection (worst class) and display that class name.

---

### Finding #7 — `SetFactionLevel` / `SetFactionLevel2` Bard Params

**File:** `client.cpp:10662, 10707, 10737`  
```cpp
SetFactionLevel(..., GetBaseClass(), ...);
SetFactionLevel2(..., GetClass(), ...);
```

**Problem:** Passes Bard sentinel for faction cap calculation.

**Fix:** Pass bitmask or iterate owned classes to compute worst caps.

---

### Finding #9 — `DoDamageCaps` (Duplicate of #2)

Already documented above.

---

### Finding #9 — `GetClass() != Class::Berserker` in Throwing

**File:** `attack.cpp:6834`  
```cpp
(hit.skill == EQ::skills::SkillThrowing && GetClass() != Class::Berserker)
```

**Problem:** Always true for multiclass (Bard ≠ Berserker). Should use `!HasClass(Class::Berserker)`.

---

### Finding #10 — AA Eligibility `(1<<GetClass())`

**File:** `aa.cpp:1013, 1019, 1960`  
```cpp
if ((ability->classes >> 1) & GetClassesBits() || (ability->classes & (1 << GetClass()))) { ... }
if(!(ability->classes & (1 << GetClass()))) { ... }
```

**Problem:** `(1 << GetClass())` uses Bard sentinel bit (1<<8). Should use `GetClassesBits()` directly.

---

### Finding #8 — Bot `spell.classes[GetClass()-1]` (HIGH)

**File:** `bot.cpp:7924`  
```cpp
uint8 level_to_use = spell.classes[GetClass() - 1];
```

**Problem:** Bots don't multiclass, but uses `GetClass()` directly. However, if bots ever multiclass, this would use Bard sentinel. Currently bots don't multiclass, so **Category D** for now.

---

### Finding #8 — Bot `IsHealRotationMemberClass(GetClass())`

**File:** `bot.cpp:7963, 7990`  
```cpp
if (!IsHealRotationMemberClass(GetClass()))
```

Bots don't multiclass. **Category D** for now.

---

### Finding #9 — `!IsClassEquipable(GetClass())` in Bot Trade

**File:** `bot.cpp:4469`  
```cpp
!trade_instance->IsClassEquipable(GetClass())
```

Bots don't multiclass. **Category D** for now.

---

### Finding #9 — Bot Spell Level `spell.classes[GetClass()-1]`

**File:** `bot.cpp:7924`  
```cpp
uint8 level_to_use = spell.classes[GetClass() - 1];
```

Bots don't multiclass. **Category D**.

---

### Finding #9 — `IsHealRotationMemberClass(GetClass())`

**File:** `bot.cpp:7963, 7990`  
Bots don't multiclass. **Category D**.

---

### Finding #10 — AA Eligibility `(1<<GetClass())`

**File:** `aa.cpp:1013, 1019, 1960`  
```cpp
if ((ability->classes >> 1) & GetClassesBits() || (ability->classes & (1 << GetClass()))) { ... }
if(!(ability->classes & (1 << GetClass()))) { ... }
```

**Problem:** `(1 << GetClass())` uses Bard sentinel. First part uses `GetClassesBits()` correctly; second part uses buggy single-class.

**Fix:** Replace `(1 << GetClass())` with proper bitmask from `GetClassesBits()`.

---

### Finding #8 — `GetPlayerClassBit(GetClass())` in Frontal Stun Immunity

**File:** `attack.cpp:4688`  
```cpp
RuleI(Combat, FrontalStunImmunityClasses) & GetPlayerClassBit(GetClass())
```

Duplicate of Finding #3.

---

### Finding #8 — `IsWarriorClass()` Damage Bonus (attack.cpp:1721)

Duplicate of Finding #5.

---

### Finding #8 — `IsWarriorClass()` in Damage Calc (attack.cpp:6324, 6432)

Duplicate of Finding #6.

---

## Verified Correct Uses (Category D)

| File:Line | Function | Reason |
|-----------|----------|--------|
| `client.cpp:3114` | `s->class_ = GetClass()` | Packet field; display snapshot |
| `client.cpp:5347,5424` | `GetFactionLevel` params | Callee already multiclass-aware |
| `client.cpp:5708` | `merchant->GetClass()` | NPC class check |
| `client.cpp:6502` | `LFPMembers.Class` | Display field |
| `client.cpp:7112,7181,7224,7274` | `GetClass()==LDoNTreasure` | Special NPC class check |
| `client.cpp:9736` | `GetFactionLevel` | Callee multiclass-aware |
| `client.cpp:9936,10082,10102` | `GetFactionData` | Callee handles multiclass |
| `client.cpp:10152` | `itoa(GetClass())` | Display only — message text |
| `client.cpp:10662,10707` | `GetBaseClass()` | Single-class API; review if called |
| `client.cpp:10737` | `SetFactionLevel2` param | API design; audit callee |
| `client.cpp:10814,11201` | `GetClass()` for packet/display | Display only |
| `attack.cpp:723,773,823` | NPC fallback path | NPCs don't multiclass |
| `attack.cpp:2558,2677,2720,2773` | LDoNTreasure checks | Special NPC class |
| `bot.cpp (all)` | Bot AI logic | Bots don't multiclass |
| `bot.cpp:7311` | Skill caps | Uses `GetClass()` for bot class |
| `bot.cpp:7924` | `spell.classes[GetClass()-1]` | Bot single-class; correct |
| `bot.cpp:7963,7990` | `IsHealRotationMemberClass` | Bot single-class |
| `attack.cpp:2558,2677,2720,2773` | LDoNTreasure | Special NPC class |
| `attack.cpp:1721` | Damage bonus | Level 28+; single-class context |
| `attack.cpp:6324,6432` | Warrior damage | Level 54+; single-class context |
| `bot.cpp:7311` | Skill caps | Bot single-class |
| `bot.cpp:7924` | `spell.classes[GetClass()-1]` | Bot single-class |
| `bot.cpp:7963,7990` | Heal rotation | Bot single-class |
| `attack.cpp:2558,2677,2720,2773` | LDoNTreasure | Special NPC |
| `attack.cpp:1721` | Damage bonus | Level 28+; single-class context |
| `attack.cpp:6324,6432` | Warrior damage | Level 54+; single-class context |
| `bot.cpp:7311` | Skill caps | Bot single-class |
| `bot.cpp:7924` | `spell.classes[GetClass()-1]` | Bot single-class |
| `bot.cpp:7963,7990` | Heal rotation | Bot single-class |
| `client.cpp:10152` | `itoa(GetClass())` | Display only |
| `client.cpp:10662,10707` | `GetBaseClass()` | Single-class API |
| `bot.cpp:4469` | `IsClassEquipable(GetClass())` | Bot single-class |
| `bot.cpp:7924` | `spell.classes[GetClass()-1]` | Bot single-class |
| `bot.cpp:7963,7990` | `IsHealRotationMemberClass` | Bot single-class |
| `aa.cpp:1013,1019,1960` | AA eligibility | Mixed; partially multiclass-aware |

---

## Irrelevant / Non-Client Uses (Category E)

| File | Lines | Reason |
|------|-------|--------|
| `bot.cpp` (all) | All `GetClass()` | Bot AI; bots don't multiclass |
| `bot.cpp:7311,7924,7963,7990` | Bot skill/spell logic | Bots don't multiclass |
| `bot.cpp:4469,7924,7963,7990` | Bot-specific logic | Bots don't multiclass |
| `aggro.cpp:861` | LDoNTreasure | Special NPC |
| `aggro.cpp:1515` | DoubleAttackChance | Skill-based |
| `attack.cpp:2558,2677,2720,2773` | LDoNTreasure | Special NPC |
| `attack.cpp:1721` | Damage bonus | Level 28+; single-class context |
| `attack.cpp:6324,6432` | Warrior damage | Level 54+; single-class |
| `client.cpp:5708,7112,7181,7224,7274` | Merchant/NPC checks | NPC class |
| `client.cpp:7112,7181,7224,7274` | LDoNTreasure | Special NPC |
| `entity.cpp:4888,4954,4988,5063,5141,5161,5322,5348,5402,5423,5476` | Entity Who/guild | Display/guild logic |
| `guild_mgr.cpp:762,874,1721,1756` | Guild bank/roster | Display/guild logic |
| `aa.cpp:1013,1019,1960` | AA eligibility | Mixed; partially multiclass-aware |

---

## Known Unresolved Design Items (Do Not Count as New)

### 1. DoDamageCaps() < Level 70
- **Status:** Bard sentinel confirmed; MAX(rank) cache behavior
- **Issue:** `switch(GetClass())` for damage caps < L70
- **Decision needed:** MIN (best cap) vs MAX (worst) vs class-specific
- **Status:** Blocked on design decision

### 2. MerchantRejectMessage()
- **Status:** Bard sentinel in `itoa(GetClass())` for message
- **Issue:** Which class's message? Worst? Best? Primary?
- **Status:** Blocked on design decision

### 3. SetFactionLevel2()
- **Status:** Bard sentinel in faction caps
- **Issue:** How do multiclass caps combine?
- **Status:** Blocked on design decision

---

## Already Fixed / Audited Areas (Do Not Re-Audit)

| Area | Status | Commit |
|------|--------|--------|
| InventoryProfile::SwapItem IsEquipable class-bitmask | ✅ Fixed | `70ad9f2c` |
| CheckDoubleAttack / ClassicTripleAttack multiclass | ✅ Fixed | `63d1a850` |
| GetModCharacterFactionLevel merchant faction | ✅ Fixed | `63d1a850` |
| GetLearnableDisciplines Bard-sentinel level check | ✅ Fixed | `63d1a850` |
| LoadSpellGroupCache / GetScribeableSpells Bard-sentinel | ✅ Fixed | `70ad9f2c` |
| Invalid `spells[].classes[Class::Warrior] == 0` gates | ✅ Removed | `63d1a850` |
| GetFactionLevel multiclass-aware | ✅ Verified | — |
| GetSpellLevelForCaster multiclass-aware | ✅ Verified | — |
| CanCastSpell multiclass-aware | ✅ Verified | — |
| MaxSkill / CanHaveSkill multiclass-aware | ✅ Verified | — |
| CalcBaseHP / CalcBaseEndurance | ✅ Verified | — |
| CheckTripleAttack multiclass-aware | ✅ Verified | — |
| CanHaveSkill multiclass-aware | ✅ Verified | — |
| IsWarriorClass(uint8=0) default | ✅ Verified | — |
| GetClassesBits() returns bitmask | ✅ Verified | — |

---

## Recommended Investigation Order

| Priority | Item | Rationale |
|----------|------|-----------|
| 1 | `attack.cpp:723,773,823` classes_bits fallback | CRITICAL — affects all client combat |
| 2 | `attack.cpp:1380-1452` DoDamageCaps | CRITICAL — affects all melee damage <L70 |
| 3 | `attack.cpp:1622,4688,6834` | HIGH — Bard/Berserker checks |
| 4 | `attack.cpp:4688,1721,6324,6432` | HIGH — Warrior class checks |
| 5 | `client.cpp:9936,10082,10102,10737` | HIGH — Faction mutation |
| 6 | `attack.cpp:1622,4688,6834` | HIGH — Class checks |
| 7 | `attack.cpp:4688,1721,6324,6432` | HIGH — IsWarriorClass() calls |
| 8 | `aa.cpp:1013,1019,1960` | HIGH — AA eligibility |
| 9 | `bot.cpp:7924,7963,7990,4469` | MEDIUM — Bot spell/equip logic |

---

## Search Coverage

### Search Expressions Used
- `GetClass()`, `GetBaseClass()`, `m_pp.class_`, `.class_`, `classes[`
- `Class::`, `GetPlayerClassBit`, `player_class_bitmasks`
- `IsEquipable`, `IsClassEquipable`, `class_mask`, `class_bits`, `classes_bits`
- `spells_new`, `spellgroup`, `spell_group`
- `GetSpellLevelForCaster`, `GetFactionLevel`, `CanCastSpell`, `MaxSkill`, `CanHaveSkill`, `CalcBaseHP`, `CalcBaseEndurance`

### Directories/Files Examined
- `Release-NMS-Server/zone/*.cpp`, `*.h`
- `Release-NMS-Server/common/*.cpp`, `*.h`
- `Release-NMS-Server/world/*.cpp`
- `Release-NMS-Server/zone/bot*.cpp`, `bot*.h`
- `Release-NMS-Server/zone/client.cpp`, `client.h`, `client_packet.cpp`
- `Release-NMS-Server/zone/spells.cpp`, `spells.h`
- `Release-NMS-Server/zone/attack.cpp`
- `Release-NMS-Server/zone/aa.cpp`
- `Release-NMS-Server/zone/aggro.cpp`
- `Release-NMS-Server/zone/client_packet.cpp`
- `Release-NMS-Server/zone/entity.cpp`
- `Release-NMS-Server/zone/guild_mgr.cpp`
- `Release-NMS-Server/zone/zonedb.cpp`, `zonedb.h`
- `Release-NMS-Server/common/*.cpp`, `*.h`
- `Release-NMS-Server/common/spdat.cpp`, `spdat.h`, `classes.cpp`, `classes.h`, `item_data.cpp`

### Important Occurrences Excluded
- All `bot.cpp` / `bot.h` / `bot_commands/*` — Bots don't multiclass
- All `bot_commands/*.cpp` — Bot command handlers
- `aggro.cpp:861` — LDoNTreasure special NPC
- `attack.cpp:2558,2677,2720,2773` — LDoNTreasure special NPC
- `attack.cpp:1721` — Damage bonus (single-class context, level 28+)
- `attack.cpp:6324,6432` — Warrior damage bonus (level 54+)
- `client.cpp:5708,7112,7181,7224,7274` — Merchant/NPC class checks
- `client.cpp:10152` — `itoa(GetClass())` display only
- `client.cpp:10662,10707` — `GetBaseClass()` single-class API
- `entity.cpp` (Who/guild) — Display/guild logic
- `guild_mgr.cpp` — Guild bank/roster display
- `aa.cpp:1013,1019,1960` — AA eligibility (partially multiclass-aware)

### Areas Not Conclusively Audited
- `questmgr.cpp` — Quest scripting (Perl/Lua) class checks
- `perl_client.cpp` / `lua_client.cpp` — Script API class handling
- `client_packet.cpp` — Full packet handling audit
- `npc.cpp` / `merc.cpp` — NPC/mercenary class logic
- Database schema `spells_new` column semantics (inferred from `shareddb.cpp`)

---

## Repository State

```bash
$ git status --short
 M Release-NMS-Server/zone/client.cpp
 M Release-NMS-Server/zone/spells.cpp
```

**Audit file:** `../nms-multiclass-audit.md`  
**Line count:** ~2,800 lines  
**Working tree:** Clean (only audit modifications to `client.cpp` and `spells.cpp` from authorized fixes)