# NMS Multiclass Foundation Audit — Canonical Record

**Repository:** Triptych Triumvirate / NMS (EQEmu 23.8.1 fork, RoF2 client)
**Branch:** `nms-development`
**Compiled by:** opencode agent synthesis (prior explore-agent passes + direct file verification)
**Last updated:** 2026-09-29 (Sweep-2 reconciliation: S-08 FIXED in `f01b999a`; S-07 FIXED in `a845bbfd`; S-06 FIXED in `d40a68f2`; S-03 FIXED in `036c2177`; S-02 CLOSED verified-correct / false positive; S-04/S-05 FIXED in `9ca593f8` — see §4.2. Prior 2026-09-28: B-1 `DoDamageCaps` fixed in `9ba735e2` §6.1; C-4 plate-user fixed in `2ce84053`; B-5 already-correct §4.1; B-6 cosmetic-only §6.2; faction write-path class-insensitive §5.3)

> **AUDIT ONLY — NO CODE MODIFICATIONS.** This document is the canonical historical record for the
> multiclass foundation audit. It is documentation only and does not change any source.
>
> Superseded audit documents are retained for history and are deprecated:
> `nms-multiclass-audit.md`, `audit_bard_attack_while_casting.md`,
> `audit_bard_attack_while_casting_RECHECK.md`. See §7 for how those files relate to this one.

---

## 1. Provenance & Purpose

This document aggregates every multiclass audit finding discovered across three explore-agent passes and
direct code verification, then reconciles them against **actual Git history** (verified this session, not
guessed). Its purposes:

1. Provide a single canonical status record for every finding, including what has since been fixed.
2. Anchor the fix ledger to real commit hashes (`git log` verified) so no dead or invented history survives.
3. Document representation invariants and semantic principles so future fixes are correct on the first try.
4. Record what remains (deferred, policy-blocked, or out of scope).

The audit covers the four subsystems planned for near-term development (spell focus effects, melee damage
caps, combat eligibility checks, AA eligibility) plus every class-bearing proc condition in the
`CalcProcConditions` switch.

### Finding Counts by Category (as originally tallied)

| Category | Count | Severity Breakdown |
|----------|-------|--------------------|
| **A** — Confirmed bug; correct semantics established by repo | 3 | 1 CRITICAL, 2 HIGH |
| **B** — Representation bug (Bard sentinel; mask semantics established) | 6 | 1 CRITICAL, 4 HIGH, 1 MEDIUM |
| **C** — Bard-sentinel incompatibility; multiclass semantics undefined | 4 | 2 CRITICAL, 2 HIGH |
| **D** — Intentional single-class / bot-only / harmless | 14 | — |
| **E** — NPC-only / display-only / irrelevant to Client gameplay | 9 | — |
| **F** — Verified already correct / already fixed | 12 | — |

**Total reachable Client gameplay occurrences audited this session:** 36
**Previously audited (carried from `nms-multiclass-audit.md`):** 164+ occurrences

> **Status note:** Since the original tally, most A/B/C findings have been FIXED (see §3 ledger) or
> formally CLOSED/RECLASSIFIED (B-3, B-5); B-1 was fixed as MAX/best-of-owned in `9ba735e2` (§6.1).
> The count table is the historical opening balance; §4 is the live status. B-1's severity is **LOW
> under the shipped config** (reachability, §6.1); the tally column above is the historical opening balance.

---

## 2. Multiclass Representation Invariants and Semantic Principles

### 2.1 Invariants (verified by direct source inspection)

| # | Invariant |
|---|-----------|
| I-1 | A **class ID** is never a class **bitmask**. The two representations are unrelated. |
| I-2 | When multiclassing is enabled, `m_pp.class_` is forced to `Class::Bard` (8) by world login (`world/client.cpp:1990`) and mirrored into zone (`client_packet.cpp:1549`). **In any Client gameplay path, `GetClass()`/`GetBaseClass()` return the Bard sentinel (8), not an owned class.** |
| I-3 | The source of truth for owned classes is `m_pp.classes`, a bitmask. Read it via `Client::GetClassesBits()` (`zone/client.cpp:14576`). |
| I-4 | `Mob::HasClass(class_id)` is mask-aware: `(GetPlayerClassBit(class_id) & GetClassesBits()) != 0` (`zone/mob.cpp:4855`). Prefer this over raw comparisons. |
| I-5 | Class bitmask values are explicit: `player_class_bitmasks[]` in `common/classes.h:79-96`, via `GetPlayerClassBit()` (`common/classes.cpp:395`). Warner on the server side: `1u << (class_id-1)`. |
| I-6 | Spell-DB and AA-ability class masks use a **different encoding**: bit `class_id` (Warrior=1 … Bard=8 → `1<<8=256`). |
| I-7 | DB `aa_ability.classes` (and the spell `classes` field) store the **item-style bitmask** (bit `class_id-1`; Warrior=1, Bard=128), default 131070 in repo (`base_aa_ability_repository.h:126` — note `65535 << 1` quirk; actual table default 65535 at manifest:1424). |
| I-8 | In memory, AA `Ability::classes` is left-shifted on load: `a->classes = e.classes << 1` (`aa.cpp:2187`) so the RoF2 client struct `AARankInfo_Struct.classes` uses bit `class_id` (Bard=256). **Because of I-8, the correct in-memory multiclass union test is `(ability->classes >> 1) & GetClassesBits()`.** |
| I-9 | `PassLimitClass(uint32 Classes_, uint16 Class_)` takes a spell-bitmask `Classes_` (Bard=256) and a raw class **ID** `Class_` (adds 1 internally). (`zone/spell_effects.cpp:8015-8031`) |

### 2.2 Core symbols

| Symbol | File | Purpose |
|--------|------|---------|
| `m_pp.class_` | `world/client.cpp:1990` | Legacy single-class field; forced to Bard sentinel. Do not treat as owned-class in Client gameplay logic. |
| `m_pp.classes` | `common/player_profile.h` | Owned-class bitmask — the source of truth. |
| `Client::GetClassesBits()` | `zone/client.cpp:14576` | Returns `m_pp.classes` (single-class fallback when multiclassing off). |
| `Mob::GetClassesBits()` | `zone/mob.cpp:4842` | Delegates to `CastToClient()->GetClassesBits()` for clients. |
| `Mob::HasClass(class_id)` | `zone/mob.cpp:4855` | Mask-aware class membership. |
| `GetPlayerClassBit(class_id)` | `common/classes.cpp:395` | Explicit map lookup `class_id → bitmask`. |
| `HasAnyClass({...})` | NMS helper | Mask-aware union test over a set of class IDs (used by the PassCastRestriction fixes, §3). |

### 2.3 Class predicates that are NOT multiclass-aware (`common/classes.cpp:404-543`)

These accept a raw `uint8 class_id` and switch on it. Passing `GetClass()` feeds the Bard sentinel and
produces wrong results for multiclass characters.

| Function | Switch matches | Bard sentinel result |
|----------|---------------|----------------------|
| `IsFighterClass(class_id)` | Warrior,Paladin,Ranger,SK,Monk,Bard,Rogue,Beastlord,Berserker | `true` (Bard=8 matches) |
| `IsNonSpellFighterClass(class_id)` | Warrior,Monk,Bard,Rogue,Berserker | `true` (Bard=8 matches) |
| `IsHybridClass(class_id)` | Paladin,Ranger,SK,Bard,Beastlord | `true` (Bard=8 matches) |
| `IsCasterClass(class_id)` | Cleric,Druid,Shaman,Necro,Wiz,Mag,Enc | `false` |
| `IsINTCasterClass(class_id)` | Necro,Wiz,Mag,Enc | `false` |
| `IsWISCasterClass(class_id)` | Cleric,Druid,Shaman | `false` |
| `IsPlateClass(class_id)` | Warrior,Cleric,Paladin,SK,Bard | `true` (Bard=8 matches) |

### 2.4 Multiclass-aware wrappers (`zone/mob.cpp:1160-1193`)

`Mob::IsWarriorClass(uint8 class_id=0)`, `IsPureMeleeClass(uint8 class_id=0)`,
`IsPriestClass(uint8 class_id=0)` — with **default `class_id=0`** they use `GetClassesBits()` and are
multiclass-aware; with a non-zero `class_id` they test a single class. **Trap:** calling
`IsWarriorClass(GetClass())` passes the Bard sentinel and is wrong; calling `IsWarriorClass()` is right.

### 2.5 Semantic principles

| Subsystem | Semantics |
|-----------|-----------|
| Eligibility (disciplines, AAs, spells, proc conditions, class limits) | **Union / any** owned class qualifies |
| HP / endurance | **Best / max** qualifying owned class |
| Mana | Best qualifying **caster** class |
| Skill caps | **Best / max** qualifying owned class |
| Melee damage caps (`DoDamageCaps`) | **Best / max** qualifying owned class — implemented `9ba735e2` (§6.1) |
| Spell required level | **Minimum** qualifying class level |
| AA eligibility | Multiclass-aware (owned classes) |
| Faction stored value | Raw accumulated personal faction relative to base; class/race/deity modifiers applied at effective-faction read time only |

Exceptions that remain unresolved by policy are flagged in §6 (END-OR-MANA proc
conditions).
B-1 (`DoDamageCaps`) was resolved by `9ba735e2` as MAX/best-of-owned — see the closed decision record §6.1.

---

## 3. Authoritative Fix Ledger (Git-Verified)

All hashes below were verified against `git log` on `nms-development` this session. Line numbers are
pre-fix snapshots where the commit is listed. **No hash in this ledger is inferred.**

| Commit | Subject | Files | What changed (multiclass-relevant) |
|--------|---------|-------|-------------------------------------|
| `17944113` | fix: correct multiclass inventory and triple attack handling | `common/inventory_profile.cpp`, `zone/attack.cpp` | `IsEquipable(race_id, classes_bits)` called with owned-class **bitmask** (was raw class ID — the A-1-style representation bug); `ClassicTripleAttack` best-of loop over owned classes. |
| `abc7826f` | fix: make merchant faction checks multiclass-aware | `zone/client.cpp` | `GetModCharacterFactionLevel` worst-of loop over owned classes (min faction to qualify any). |
| `63d1a850` | fix: make discipline learning multiclass-aware | `zone/client.cpp` | `GetLearnableDisciplines` uses `GetSpellLevelForCaster(spell_id)`; 255 = not learnable. |
| `70ad9f2c` | fix: make spell group scribing multiclass-aware | `zone/client.cpp`, `zone/spells.cpp` | `LoadSpellGroupCache` builds OR'd SQL class filter from `GetClassesBits()`; new `GetSpellLevelForCaster` helper in `spells.cpp`. |
| `e008d038` | fix: remove invalid spell class eligibility gates | `zone/client.cpp` | Removed `spells[].classes[Class::Warrior] == 0` invalid gates that broke scribability. |
| `2690ef5f` | fix: allow multiclass bards to attack while casting | `zone/attack.cpp` | `attack.cpp:1622` `IsCasting() && GetClass() != Class::Bard` → `IsCasting() && !HasClass(Class::Bard) && !IsFromSpell`. This resolved **B-2** and the original “bard attack while casting” audits. |
| `a2dc4ac3` | fix: make spell class limits multiclass-aware | `zone/spell_effects.cpp` | Three hunks: `CalcAAFocus` (~5436), `CalcFocusEffect` (~6153), and the caster path (~6226). `PassLimitClass(base_value, GetClass())` replaced with an any-class-passes loop over owned classes (`GetClassesBits()`). Resolved **A-1/A-2/A-3**. |
| `92cfd33c` | fix: make berserker throwing bonus multiclass-aware | `zone/attack.cpp` | `GetClass() != Class::Berserker` → `!HasClass(Class::Berserker)` in the throwing damage halving. Resolved **B-4**. |
| `7d328cf4` | fix: sync multiclass berserker throwing tuning | `zone/tune.cpp` | Endurance/throwing tuning uses `!HasClass(Class::Berserker)` (companion to `92cfd33c`). |
| `974cb171` | fix: make spell cast restrictions multiclass-aware | `zone/spell_effects.cpp` | PassCastRestriction, **5 cases** (first batch). Exact rewrites are reproduced in §5.1. Resolved **C-1, C-2, C-3**, and half of **C-4**. |
| `b1aa4b5d` | fix: make additional cast restrictions multiclass-aware | `zone/spell_effects.cpp` | PassCastRestriction, **4 more cases** (second batch). Resolved the remaining **C-4** cast-restriction conditions. |
| `2ce84053` | fix: make Jann class restrictions multiclass-aware | `zone/spell_effects.cpp` | PassCastRestriction plate-user Jann set, **4 cases**: `IS_CLIENT_AND_MALE/FEMALE_PLATE_USER` (11044/11210) replace `IsPlateClass(GetClass())` with `HasAnyClass({Warrior, Cleric, Paladin, ShadowKnight, Bard})`; `IS_CLEINT_AND_MALE/FEMALE_DRUID_ENCHANTER_MAGICIAN_NECROANCER_SHAMAN_OR_WIZARD` (11090/11211) replace `(IsCasterClass(GetClass()) && GetClass() != Class::Cleric)` with `HasAnyClass({Druid, Shaman, Necromancer, Wizard, Magician, Enchanter})`. `IsClient()` + gender gates preserved; non-Client behavior unchanged. Resolved the plate-user **C-4** set (§4/§6.3). |
| `e647073d` | docs: add NMS development guidance | `AGENTS.md` | Documentation only; records the multiclass architecture. Not a code fix. |
| `9ba735e2` | fix: make damage caps multiclass-aware | `zone/attack.cpp` | B-1: `DoDamageCaps` now selects the highest damage-cap tier among owned classes via `GetClassesBits()` + `HasClass(i, classes_bits)` + `std::max`; the per-class/per-level cap table is factored verbatim into the file-scope `DoDamageCapByClass` helper; zero-mask fallback keeps the legacy `default` tier; the `LevelToStopDamageCaps` early return and final `std::min` are preserved. Resolved **B-1**. |
| `9ca593f8` | fix: make spell-level lookups multiclass-aware | `zone/effects.cpp`, `zone/mob.cpp` | S-04: `GetActSpellDamage` crit extra-spell-damage level restriction (`effects.cpp:320`) and `GetActSpellCost` Clairvoyance restriction (`effects.cpp:836`) — raw `spells[].classes[(GetClass()%17)-1]` replaced with `GetSpellLevelForCaster(spell_id)`; `>= GetLevel() - 5` comparison unchanged. S-05: `GetDecayEffectValue` (`mob.cpp:7509`) — `IsClient() ? GetSpellLevelForCaster(spell_id) : spells[spell_id].classes[(GetClass()%17)-1]`; non-Client path preserved. Single-class behavior byte-identical. **Does not include S-02** — no source change was required (§4.2). |
| `036c2177` | fix: use trainer class for new skill training | `zone/client_process.cpp` | S-03: `Client::OPGMTrainSkill` new-skill branch (`client_process.cpp:2136`) — `GetSkillTrainLevel(skill, GetClass())` → `GetSkillTrainLevel(skill, trains_class)`. One line, one tracked file (1 insertion / 1 deletion); the trainer NPC's class is the class governing this transaction, and both the listing path and the `GetPlayerClassBit(trains_class)` ownership gate already use it, so the lookup no longer probes the Bard sentinel. Shipped-data effect: **Skill 77 / `Skill2HPiercing`** (Bard has no `skill_caps` row; Warrior/Paladin/Ranger/ShadowKnight/Berserker do). **Does not include** the Perl/Lua `GetSkillTrainLevel` wrappers or `SkillCaps::GetSkillTrainLevel`, both of which remain unchanged (§4.2/§9). Resolved **S-03**. |
| `d40a68f2` | fix: restore multiclass max skills | `zone/client.cpp` | S-06: `Client::MaxSkills()` (`client.cpp:13849-13885`) — three repairs in one hunk (8 insertions / 1 deletion, one tracked file). (1) Restored the missing accumulator `highestSkillCap = std::max(highestSkillCap, classSkillCap);`, so the multiclass bit-walk takes **MAX/best-of-owned** instead of leaving the cap at 0. (2) Restored the missing non-multiclass `else` path using `GetClass()` + `GetLevel()`. (3) Restored current-level semantics: `GetSkillCap(classID, s.first, RuleI(Character, MaxLevel))` → `GetSkillCap(classID, s.first, GetLevel())`. Final `GetSkill(s.first) < highestSkillCap` guard, `SetSkill`, and the `sizeof(classes_bits) * 8` loop are unchanged; class IDs 17..32 still safely yield cap 0. Specialization behavior preserved (`MAX_SPECIALIZED_SKILL`); **no** `CanHaveSkill()` guard and **no** zero-mask fallback were added, per canonical upstream behavior. **Does not include** the Perl/Lua `MaxSkills` bindings or the GM command path, both unchanged. Resolved **S-06**. |
| `a845bbfd` | fix: make guild bank equip checks multiclass-aware | `zone/guild_mgr.cpp` | S-07: three guild-bank `IsEquipable` class-argument repairs (3 insertions / 3 deletions, one tracked file, one commit). `EQ::ItemData::IsEquipable(uint16 race_id, uint16 class_bits)` (`common/item_data.cpp:172`) has always required a class **bitmask**; three guild-bank callers instead passed a numeric class **ID** — the same representation bug already fixed for the equip path in `17944113` (§3). (1) `SendGuildBank` deposit-area list (`guild_mgr.cpp:762`, **LIVE** for the deployed RoF2 client): `c->GetBaseClass()` → `c->GetClassesBits()`. (2) `SendGuildBank` pre-RoF main-area list (`guild_mgr.cpp:874`, **DORMANT** for RoF2 — the RoF-and-later path returns before the pre-RoF branch, `742`/`797`; corrected anyway so the whole `SendGuildBank` family keeps the proper representation): `c->GetBaseClass()` → `c->GetClassesBits()`. (3) `SendGuildBankItemUpdate` (`guild_mgr.cpp:1721`, **LIVE**, fires on every add/withdraw/promote/permission/split/merge broadcast): `c->GetClass()` → `c->GetClassesBits()`. **Already correct and not modified:** `guild_mgr.cpp:787` (main-area list, LIVE) already used `c->GetClassesBits()` — its inclusion as defective in the initial sweep was a **false positive**. **Not multiclass-only:** because a numeric class ID is generally not the corresponding class bitmask, the defect also hit ordinary single-class clients — 14 of 16 classes tested the wrong bit (Warrior and Cleric were the two numerically self-consistent cases); that blast radius is part of this finding, not a separate one. **Semantics restored:** ANY-owned/union eligibility, matching the authoritative equip path (`common/inventory_profile.cpp:348,382` via `zone/inventory.cpp:2404`) and the pre-existing `787` precedent. `GetClassesBits()` supplies the proper class bit with multiclassing disabled, the one owned-class bit with a single owned class, and the OR of owned-class bits under true multiclassing. **Race arguments deliberately unchanged** — `GetBaseRace()` at 762/787/874, `GetRace()` at 1721. **Impact LOW / UI-only:** this sets the guild-bank usable/display indication. It is **not** server-side guild-bank permission enforcement, item integrity, or a security boundary — `DeleteItem` (`1168`) has no usable/permission check, and `GuildBankPublicIfUsable` (`guild_mgr.h:59`) is declared but never evaluated server-side. **Does not include:** `ItemData::IsEquipable`, `IsClassEquipable`, `Client::GetClassesBits`, guild-bank permissions, packet structures, race semantics, `RemoveExtraClass` (`client.cpp:14722`), `Mob::CanClassEquipItem` (`mob.cpp:7945-7951`, no in-repo callers), the Perl `GetClassBitmask` drift, or any other equip-related sibling finding. Resolved **S-07**. |
| `f01b999a` | fix: make who class filters multiclass-aware | `common/servertalk.h`, `zone/client.cpp`, `zone/entity.cpp`, `world/cliententry.h`, `world/cliententry.cpp`, `world/clientlist.cpp` | S-08: the **five** `/who` class-filter predicates (zone `EntityList::ZoneWho` count + emit; world `ClientList::SendWhoAll` count + emit; `ClientList::ConsoleSendWhoAll`) compared the requested class against the **Bard sentinel** field rather than owned-class membership. Transport/storage: appended `uint32 classes_bits` to the **end** of `ServerClientList_Struct` (238 → **242** bytes, field at **offset 238**, prior packed offsets unchanged, no field reordered); `Client::UpdateWho` now publishes both `s->class_` (retained for legacy/display consumers) and `s->classes_bits = GetClassesBits()`; `ClientListEntry` stores the mask (`m_classes_bits` + `classes_bits()`, set in `Update`, zeroed in `ClearVars`). Semantics: one shared zone lambda and one shared world/console `WhoHasClass` helper, each **range-checking the full-width class ID before `static_cast<uint8>`**, restoring **ANY-owned / union** membership with no manual bit shift; `0xFFFFFFFF`/`0xFFFF` no-class-filter sentinels and all anon/admin predicates unchanged; `classes_bits == 0` matches no class-specific filter and gets **no** Bard/`GetClass()` fallback. Also adds success-path `UpdateWho()` to `Client::AddExtraClass`/`RemoveExtraClass` so world `/who` state cannot go stale after an online class change (all failure/no-op paths still return before mutation). Six files, 50 insertions / 5 deletions. Full Release build successful; `sizeof = 242` / `offsetof = 238` confirmed by a throwaway `cl.exe` probe. **No live in-game test performed.** Does **not** include the LFG class-mask/sentinel sites, console multiclass **display**, or a `GestaltClasses` bucket fallback/migration — all three remain unresolved adjacent findings (§4.2). Resolved **S-08**. |

Combined, these commits close **every A/B-1/B-2/B-4/B-5, all C-1/C-2/C-3**, the **cast-restriction C-4**
occurrences, and the **plate-user C-4 (Jann/illusion) set** (`2ce84053`), plus the Sweep-2 spell-level
lookups (S-04/S-05, `9ca593f8`), the Sweep-2 trainer new-skill lookup (S-03, `036c2177`), the guild-bank
`IsEquipable` class-argument repair (S-07, `a845bbfd`), the `MaxSkills` restoration (S-06, `d40a68f2`), and the
`/who` class-filter repair across all five filter predicates (S-08, `f01b999a`). S-02 is
CLOSED verified-correct with no source change (§4.2). Remaining open items are in §6, §4.2
(S-01/S-09..S-10), and §9.

---

## 4. Finding Status Matrix

### A — Confirmed bug; correct semantics established into fix

| Find | Site | Status | Evidence |
|------|------|--------|----------|
| A-1 (CRITICAL) | `spell_effects.cpp` SE_LimitClass in `CalcFocusEffect` | **FIXED** | `a2dc4ac3` |
| A-2 (HIGH) | `spell_effects.cpp` SE_LimitClass in `CalcAAFocus` | **FIXED** | `a2dc4ac3` |
| A-3 (HIGH) | `spell_effects.cpp` SE_LimitClass in caster path of `CalcFocusEffect` | **FIXED** | `a2dc4ac3` |

> Prior reading: `GetClass()` (Bard sentinel) fails every SE_LimitClass check against non-Bard classes.
> Correct semantics are UNION — a focus/AA effect qualifies if ANY owned class matches the limit. The
> fix implements exactly that via an owned-class loop.

### B — Representation bug (Bard sentinel)

| Find | Site | Status | Evidence |
|------|------|--------|----------|
| B-1 (LOW) | `attack.cpp:1471` `DoDamageCaps` (helper `DoDamageCapByClass` `1363-1469`; pre-fix switch `1380-1452`) | **FIXED** | `9ba735e2` — MAX/best-of-owned cap over `GetClassesBits()`; cap table, `Combat:LevelToStopDamageCaps`, callers, and NPC/pet/merc reachability unchanged. Severity **LOW under the shipped config** (rule = 20 → caps execute only at levels 1–19). See §6.1. |
| B-2 (HIGH) | `attack.cpp:1622` `GetClass() != Class::Bard` attack-while-casting | **FIXED** | `2690ef5f` → `!HasClass(Class::Bard)`. |
| B-3 (HIGH) | `attack.cpp:4688` `FrontalStunImmunityClasses & GetPlayerClassBit(GetClass())` | **RECLASSIFIED → E (NPC-only); verified correct** | Frontal stun immunity is evaluated for the **NPC** caster/attacker, not the multiclass player; and `Mob::CheckFrontalStun` at this site is an NPC combat path. No fix needed. |
| B-4 (HIGH) | `attack.cpp:6834` throwing halving `GetClass() != Class::Berserker` | **FIXED** | `92cfd33c` (+ `7d328cf4` tune sync). |
| B-5 (HIGH) | `aa.cpp:1013, 1019, 1960` raw `(1 << GetClass())` | **CLOSED — already correct; benign display redundancy** | See §4.1. |
| B-6 (MEDIUM) | `client.cpp:10152` `MerchantRejectMessage` `itoa(GetClass())` | **OPEN — policy/design deferred (cosmetic)** | §6.2. Verified 2026-09-28: message-selection/display only; merchant-open + item gates already multiclass-aware; cannot permit/deny transactions. |

### C — Semantic-undefined findings (proc / cast-restriction conditions)

| Find | Sites | Status | Evidence |
|------|-------|--------|----------|
| C-1 (CRITICAL) | `IS_CLASS_MELEE_THAT_CAN_BASH_OR_KICK_EXCEPT_BARD` | **FIXED** | `974cb171` → `HasAnyClass({Warrior, Paladin, Ranger, ShadowKnight, Monk, Beastlord, Berserker})`. |
| C-2 (CRITICAL) | `IS_CLASS_NOT_WAR_PAL_SK` | **FIXED** | `974cb171` → `!HasAnyClass({Warrior, Paladin, ShadowKnight})`. |
| C-3 (HIGH) | `IS_CLASS_PURE_CASTER` | **FIXED** | `974cb171` → `HasAnyClass({Necromancer, Wizard, Magician, Enchanter})`. |
| C-4 (HIGH) | `IS_CLASS_HYBRID_CLASS`, `IS_CLASS_CLR_SHM_DRU` | **FIXED** | `974cb171` → `HasAnyClass({Paladin, Ranger, ShadowKnight, Bard, Beastlord})` and `HasAnyClass({Cleric, Druid, Shaman})`. |
| C-4 (HIGH) | `IS_CLASS_KNIGHT_HYBRID_MELEE`, `IS_CLASS_WARRIOR_CASTER_PRIEST`, `IS_CLASS_CASTER_PRIEST`, `IS_NOT_CLASS_BARD` | **FIXED** | `b1aa4b5d` — exact sets in §5.1. |
| C-4 (HIGH) | **Plate-user** `IS_CLIENT_AND_MALE/FEMALE_PLATE_USER` + `IS_CLEINT_AND_MALE/FEMALE_DRUID_ENCHANTER_MAGICIAN_NECROANCER_SHAMAN_OR_WIZARD`, `spell_effects.cpp:9009-9031` | **FIXED** | `2ce84053` → `HasAnyClass({Warrior, Cleric, Paladin, ShadowKnight, Bard})` / `HasAnyClass({Druid, Shaman, Necromancer, Wizard, Magician, Enchanter})`. See §6.3. |
| C-4 (MEDIUM) | **END-OR-MANA** `IS_END_OR_MANA_ABOVE_20_PCT` (9077), `..._BELOW_10_PCT` (9086), `..._BELOW_30_PCT`/`_2` (9098-9099) | **DEFERRED — confirmed latent** | §6.4. |

### D, E, F — no-change categories

Retained from the original tally; see §5 (verified-correct) and §7 for traps. D (14) items are
intentional/display/NPC/bot; E (9) are NPC/display-only; F (12) verified already correct.

---

## 4.1 B-5 — FINAL DISPOSITION (already correct; benign display redundancy)

**Verdict: NOT A BUG. No source fix required.** Proven by full call-path tracing and git blame.

Encoding, verified:

- DB `aa_ability.classes` = **item-style player-class bitmask** (bit `class_id-1`; Warrior=1 … Bard=128).
  Repo default constant is `131070` (`base_aa_ability_repository.h:126`, the `65535 << 1` quirk); the
  actual DB table default is `65535` (manifest:1424).
- On load, `aa.cpp:2187` does `a->classes = e.classes << 1`, converting to the RoF2 client encoding
  (`AARankInfo_Struct.classes`, bit `class_id`; Bard=256). **The in-memory mask is client-encoding.**
- Therefore the correct mask-aware union test is `(ability->classes >> 1) & GetClassesBits()` — which is
  exactly what the authoritative gate uses.

Site-by-site:

| Site | Function | What it does | Verdict |
|------|----------|--------------|---------|
| `aa.cpp:1956` | `Mob::CanUseAlternateAdvancementRank` — **authoritative gate** | `(a->classes >> 1) & GetClassesBits()` | **CORRECT** mask-aware union. This gate already drives `GetMaxLevel` (`aa_ability.cpp:45`), activation (`aa.cpp:1581`), refund pruning (`aa.cpp:933`), purchase commands (`aa.cpp:2566, 2625, 2680`). |
| `aa.cpp:1013` | `Client::SendAlternateAdvancementRank` (display list) | First clause correct; second OR clause `(ability->classes & (1 << GetClass()))` | **Benign redundancy** under multiclassing: `GetClass()` = Bard sentinel 8 → `ability->classes & 256`. Over-matches, but the same function re-validates via `CanUseAlternateAdvancementRank` (line 1026) before the packet is sent. Display-only. |
| `aa.cpp:1019` | `SendAlternateAdvancementRank` **else** (multiclass off) branch | `(ability->classes & (1 << GetClass()))` | **Correct** for single-class; unreachable when multiclassing is enabled. |
| `aa.cpp:1960` | `CanUseAlternateAdvancementRank` **else** (multiclass off) branch | `(ability->classes & (1 << GetClass()))` | **Correct** for single-class; unreachable when multiclassing is enabled. |

Provenance: `git blame` attributes lines 1011-1024 and 1945-1963 to `^fedaa3c5` (tunaria, 2026-08-04),
in which the multiclass-aware union (`>> 1`) gate was introduced. The AA subsystem has been multiclass
correct since that commit; the remaining `(1 << GetClass())` occurrences are single-class fallbacks or a
neutralized display redundancy, **not** a live bug.

**Optional cleanup is cosmetic only** (e.g., removing the redundant OR clause at `aa.cpp:1013`, or
swapping the two else-branch decorations for a comment) and is explicitly **not** performed as part of
this audit.

---

### 4.2 Sweep 2 — spell-level lookups & base_data regen (2026-09-29)

Sweep-2 candidates surfaced by the automated code sweep are tracked as **S-01..S-10**. This table is
the **live status for the S-series**; it is not part of the historical A/B/C tally — the §1 counts and
the §4 A/B/C/D/E/F matrix are unchanged.

| Sweep-2 ID | Site (HEAD `f01b999a`) | Status | Details |
|------------|------------------------|--------|---------|
| S-02 | `zone/client_mods.cpp:240-243` (`CalcHPRegen`), `1709-1713` (`CalcEnduranceRegen`) | **VERIFIED CORRECT / FALSE POSITIVE — CLOSED (no source change)** | Both consumers read only `base_data.hp_regen` / `base_data.end_regen`. Column order `base_base_data_repository.h:55-68`. Shipped data re-verified 2026-09-29 from `release-peq.zip`: **1600 rows (100 levels × 16 classes), exactly one `(hp_regen,end_regen)` pair per level** (L1=2/5, L75=9/20, L100=14/24). The Bard-sentinel `GetClass()` row lookup cannot change the result. Columns are upstream renames of `unk1`/`unk2` (`database_update_manifest.cpp:5266-5268`). **Latent hazard:** becomes multiclass-sensitive *only if* these fields are ever made class-dependent. |
| S-03 | `zone/client_process.cpp:2136` (`Client::OPGMTrainSkill` new-skill branch) | **FIXED** — `036c2177` | `GetSkillTrainLevel(skill, GetClass())` → `GetSkillTrainLevel(skill, trains_class)`; one line, one tracked file (1+/1-). **`trains_class` is authoritative** because it is derived from the trainer NPC, constrained to a playable class (`WarriorGM`(20)−`Warrior`(1) → [1,16]), and is already the class this purchase path ownership-verifies at `2138` via `GetPlayerClassBit(trains_class)`; the `OPGMTraining` listing path is likewise built on it (`1992`, `2001`, `2007`, `2015`). Under the Bard sentinel the old form returned **0** for **Skill 77 / `Skill2HPiercing`** (Bard has no `skill_caps` row for it at any level) while **Warrior, Paladin, Ranger, ShadowKnight, Berserker** have a positive **level-85** row, so the old form produced `SetSkill(77, 0)` with the training point still consumed; the result is now `SetSkill(77, 1)` and point consumption is unchanged. Shipped-data probe level is **85**, not 75 (`Character:MaxLevel=85`, `Character:SkillCapMaxLevel=-1` ⇒ `skill_cap_max_level`=85); the complete 123,610-row recheck at level 85 still finds exactly one affected skill. Single-class behavior unchanged — a legitimate transaction has `trains_class == GetClass()`. No Bot/Merc/NPC path is affected. **Deliberately not changed:** `SkillCaps::GetSkillTrainLevel` (`common/skill_caps.cpp:51-71`) keeps its exact-level-row existence-test behavior and effectively returns 0/1 — **not repaired**; and the Perl/Lua `GetSkillTrainLevel` wrappers (`perl_client.cpp:3416-3418`, `lua_client.cpp:3518-3521`) still pass `self->GetClass()` and **remain an unresolved scripting-surface follow-up** (no trainer-class context; separate API/semantic decision). |
| S-04 | `zone/effects.cpp:320` (`GetActSpellDamage` crit extra-spell-damage level restriction); `zone/effects.cpp:836` (`GetActSpellCost` Clairvoyance restriction) | **FIXED** — `9ca593f8` | Raw `spells[].classes[(GetClass()%17)-1]` replaced with `GetSpellLevelForCaster(spell_id)` → earliest/minimum spell level among owned classes. `>= GetLevel() - 5` comparison unchanged; no unrelated spell-damage formulas touched; single-class behavior preserved (single-bit mask → same column). |
| S-05 | `zone/mob.cpp:7509` (`GetDecayEffectValue`) | **FIXED** — `9ca593f8` | Guarded: `IsClient() ? GetSpellLevelForCaster(spell_id) : spells[spell_id].classes[(GetClass()%17)-1]`. Multiclass Clients now use earliest owned-class level; **all non-Client entities deliberately retain the legacy lookup.** The raw `classes[(GetClass` occurrence inside this ternary is **intentional, not residual**. |
| S-06 | `zone/client.cpp:13849-13885` (`Client::MaxSkills`) | **FIXED** — `d40a68f2` | **Triptych multiclass regression of inherited canonical EQEmu `MaxSkills()`** — not a base-EQEmu defect, not unknown-provenance, not a new feature, not a policy redesign. Canonical v23.8.1 already shipped a working single-class form (`IsSpecializedSkill ? MAX_SPECIALIZED_SKILL : GetSkillCap(GetClass(), s.first, GetLevel()).cap`, then `if (GetSkill(...) < value) SetSkill(...)`); the Triptych bit-walk introduced **three** regressions: **(a)** the accumulator `highestSkillCap = std::max(highestSkillCap, classSkillCap);` was dropped, **(b)** the non-multiclass `else` path was dropped, **(c)** the cap level argument was changed from `GetLevel()` to `RuleI(Character, MaxLevel)`. **Pre-fix behavior:** the missing accumulator left `highestSkillCap` at 0 in multiclass mode, and the missing non-multiclass path left it at 0 with multiclassing disabled; consequently the final `GetSkill < highestSkillCap` guard performed **no** skill writes and the function was a complete no-op. `Character:MaxLevel` was therefore a **latent** semantic defect, not an observable over-cap behavior — repairing accumulation while keeping it *would* have granted maximum-level caps. Shipped-data probe: level-30 Warrior 1H Blunt caps at **170** current-level vs **355** at level 85, so `d40a68f2` restored upstream `GetLevel()` rather than preserving that argument. **Combination rule:** restoration plus the established NMS multiclass numeric-cap semantic — `MAX(cap for each actually-owned class at current level)`, consistent with `Client::MaxSkill()`; not SUM, MIN, or melee-wins, and not an open policy choice. **Specialization/eligibility preserved:** `MAX_SPECIALIZED_SKILL` (50) is inherited canonical behavior, not a conversion artifact; canonical `MaxSkills` never called `CanHaveSkill()`, and an unavailable class/skill pair yields cap 0, which the unsigned final comparison cannot write. No `CanHaveSkill()` guard and no zero-mask fallback were added. One hunk, 8 insertions / 1 deletion. **Reachability:** GM `#maxskills` / `skill_all_max` plus the exposed Perl and Lua Client bindings; no shipped quest/plugin caller and no ordinary gameplay path (login, level-up, combat, trainer). A real functional defect, GM/script-surface limited. |
| S-07 | `zone/guild_mgr.cpp:762` (`SendGuildBank` deposit-area list, **LIVE** for RoF2), `874` (`SendGuildBank` pre-RoF main-area list, **DORMANT** for RoF2), `1721` (`SendGuildBankItemUpdate`, **LIVE**); `787` (`SendGuildBank` main-area list, **LIVE**, already correct) | **FIXED** — `a845bbfd` | **Triptych class-ID → class-bitmask conversion regression**, not a base-EQEmu defect: the callee `EQ::ItemData::IsEquipable(uint16 race_id, uint16 class_bits)` (`common/item_data.cpp:172`) already expected a class **bitmask**, while three stale callers passed numeric class **IDs** (`GetBaseClass()` at 762/874, `GetClass()` at 1721) — the same representation bug fixed for the equip path in `17944113` (§3). `787` already passed `GetClassesBits()` and was a **false positive** from the initial sweep; **not** modified by `a845bbfd`. `a845bbfd` changed only the three class arguments (3 insertions / 3 deletions). **Not multiclass-only:** a numeric class ID is generally not the corresponding class bitmask, so ordinary single-class clients were affected too — 14 of 16 classes could test the wrong class bit; Warrior and Cleric were the two numerically self-consistent exceptions. That single-class blast radius is recorded here rather than as a separate finding. **Restored semantics: ANY-owned / union** item eligibility, identical to the authoritative equip gate (`common/inventory_profile.cpp:348,382` via `zone/inventory.cpp:2404`) and to the pre-existing `787` precedent: multiclassing disabled → the proper bit for the client's real class; one owned class → that one owned-class bit; true multiclass → the OR of owned-class bits. **Race arguments and race semantics deliberately unchanged** — `GetBaseRace()` at 762/787/874, `GetRace()` at 1721; no race behavior was altered. **Reachability:** 762 and 1721 are live for the deployed RoF2 client; 874 is dormant (the RoF-and-later `SendGuildBank` path returns at 797 before the pre-RoF branch) and was corrected anyway for family consistency. **Severity LOW / UI-only** — the guild-bank usable/display indication only. **Not** server-side guild-bank permission enforcement (`DeleteItem` at 1168 performs no usable/permission check; `GuildBankPublicIfUsable` at `guild_mgr.h:59` is declared but never evaluated server-side), **not** item integrity, **not** an item-loss/duplication issue, **not** a security boundary. **Does not include:** `ItemData::IsEquipable`, `IsClassEquipable`, `Client::GetClassesBits`, guild-bank permissions, packet structures, the authoritative inventory equip path, `RemoveExtraClass` (`client.cpp:14722`), `Mob::CanClassEquipItem` (`mob.cpp:7945-7951`, no in-repo callers), the Perl `GetClassBitmask` drift, or other equip-related sibling findings. |
| S-08 | `zone/entity.cpp:4885-4890` (`who_class_match` lambda) + `:4901` / `:4967` (`EntityList::ZoneWho` count + emit passes); `world/clientlist.cpp:553-559` (`WhoHasClass` helper) + `:623` / `:721` (`ClientList::SendWhoAll` count + emit) + `:1160` (`ClientList::ConsoleSendWhoAll`); transport/storage support in `common/servertalk.h:604`, `zone/client.cpp:3114-3118`, `world/cliententry.h:88,149`, `world/cliententry.cpp:196,256` | **FIXED** — `f01b999a` | **Root defect:** all five `/who` class-filter predicates compared the requested class against `class_` / `GetClass()`, which yields the **Bard sentinel** when multiclassing is enabled — so filtering tested the sentinel instead of owned-class membership. **Transport:** world-side filtering could not be repaired from the existing payload, which carried only `uint8 class_`; `uint32 classes_bits` was **appended to the end** of `ServerClientList_Struct` (old 238 → **242** bytes, field at **offset 238**; all prior packed offsets unchanged, no field reordered). The receiver already validates with `sizeof(ServerClientList_Struct)`; zone and world were rebuilt from the same header. `Client::UpdateWho` now publishes **both** `s->class_ = GetClass()` (deliberately retained for legacy/display consumers) and `s->classes_bits = GetClassesBits()`. **World storage:** `ClientListEntry::m_classes_bits` + `classes_bits()`, set from `scl->classes_bits` in `Update` and zeroed in `ClearVars`. **Zone semantics:** one shared `who_class_match` lambda serves both passes, so the count and emit passes cannot diverge and desynchronise `Entries`/`PacketLength` from the emitted records; it **range-checks the full-width `wclass` before `static_cast<uint8>`**, accepts only `Class::Warrior`..`Class::Berserker`, then calls `HasClass(...)`; no `GetClass()` equality, no manual bit shift. **World/console semantics:** one shared `WhoHasClass` helper serves all three sites, range-checking the full-width `uint16` before narrowing and then testing `GetPlayerClassBit(class ID) & cle.classes_bits()`. **Sentinels preserved:** `0xFFFFFFFF` (zone) and `0xFFFF` (world/console) no-class-filter sentinels unchanged; `SendWhoAll` anon/admin predicates unchanged; `ConsoleSendWhoAll` keeps its prior visibility behavior. **Invalid-filter safety:** because the range check precedes narrowing, class `0`, class `17+`, and out-of-range values such as `257` match no class-specific filter instead of aliasing onto a real class ID. **Zero mask:** `classes_bits == 0` matches **no** class-specific filter (unfiltered `/who` unaffected); S-08 deliberately adds **no** fallback to Bard, `GetClass()`, or a guessed base class. **Semantics restored: ANY-owned / union** membership — single-class behavior preserved for all 16 playable classes via `GetPlayerClassBit(class ID)`; Warrior+Wizard matches Warrior and Wizard, Cleric+Wizard matches Cleric and Wizard, and so on. **Online class-mutation refresh:** because world filtering now depends on the transported mask, success-path `UpdateWho()` calls were added to `Client::AddExtraClass` and `Client::RemoveExtraClass` so world `/who` state cannot remain stale after an online class change; every `return false` / no-op path still returns before mutation, `m_pp.classes` is assigned and `SetBucket` called first, and no later observable persistence failure or rollback precedes `return true`. **Verification:** `git diff --check` clean; full Release build successful (`world.exe` and `zone.exe` linked, all nine configured binaries rebuilt); a throwaway `cl.exe` probe against the real header confirmed `sizeof = 242` / `offsetof = 238`; only pre-existing unrelated MSVC `C4244` warnings. **No live in-game verification was performed** — no running environment was available. **Console class FILTERING is fixed; console multiclass class DISPLAY is not** (see the adjacent residual note below). **Adjacent findings deliberately excluded from `f01b999a`, all still unresolved and not to be read as fixed:** (1) **LFG class-mask/sentinel behavior** in `world/clientlist.cpp` — the existing `1 << CLE->class_()` sites and the response publication `Buffer->Class_ = CLE->class_()` are unchanged; (2) **console multiclass display** still renders through the sentinel-backed `GetClassIDName(cle->class_(), ...)` path, so a multiclass character may still print as **BRD** in console output even though filtering is now mask-aware; (3) **missing `GestaltClasses` data-bucket fallback/migration** — no migration was added, so a character whose bucket is absent or unparsable still has `classes_bits == 0` and matches no class-specific filter. These three were discovered while closing S-08 and were deliberately left out of the commit; they have not been promoted into independently approved implementation items. |
| S-01 | Resist final bonuses (`zone/client_mods.cpp`: `CalcMR` 1088 already mask-aware; `CalcFR` 1161, `CalcDR` 1247, `CalcPR` 1339, `CalcCR` 1431 sentinel-gated) | **OPEN — policy required (MAX vs SUM)** | Disposition unchanged; not part of `9ca593f8`. See §9. |
| S-09..S-10 | Sweep-2 candidates (`/setstartcity`, SK/Pal refresh) | **OPEN — not yet resolved** | Disposition unchanged. See §9. |

---

## 5. Verified Correct / No-Change Sites

### 5.1 PassCastRestriction — exact rewrites from `974cb171` and `b1aa4b5d` (reference)

Batch 1 (`974cb171`):
| Restriction ID | Old expression | New expression |
|----------------|----------------|----------------|
| `IS_CLASS_MELEE_THAT_CAN_BASH_OR_KICK_EXCEPT_BARD` | `GetClass() != Class::Bard && GetClass() != Class::Rogue && IsFighterClass(GetClass())` | `HasAnyClass({Warrior, Paladin, Ranger, ShadowKnight, Monk, Beastlord, Berserker})` |
| `IS_CLASS_PURE_CASTER` | `IsINTCasterClass(GetClass())` | `HasAnyClass({Necromancer, Wizard, Magician, Enchanter})` |
| `IS_CLASS_HYBRID_CLASS` | `IsHybridClass(GetClass())` | `HasAnyClass({Paladin, Ranger, ShadowKnight, Bard, Beastlord})` |
| `IS_CLASS_CLR_SHM_DRU` | `IsWISCasterClass(GetClass())` | `HasAnyClass({Cleric, Druid, Shaman})` |
| `IS_CLASS_NOT_WAR_PAL_SK` | `GetClass() != Warrior && != Paladin && != SK` | `!HasAnyClass({Warrior, Paladin, ShadowKnight})` |

Batch 2 (`b1aa4b5d`):
| Restriction ID | Old expression | New expression |
|----------------|----------------|----------------|
| `IS_CLASS_KNIGHT_HYBRID_MELEE` | `IsKnightOrHybridOrMeleeClass(GetClass())` | `HasAnyClass({Warrior, Paladin, Ranger, ShadowKnight, Monk, Bard, Rogue, Beastlord, Berserker})` |
| `IS_CLASS_WARRIOR_CASTER_PRIEST` | `HasClass(Warrior) && IsCasterClass(GetClass())` compositions | `HasAnyClass({Warrior, Cleric, Druid, Shaman, Necromancer, Wizard, Magician, Enchanter})` |
| `IS_CLASS_CASTER_PRIEST` | `IsCasterClass(GetClass())` | `HasAnyClass({Cleric, Druid, Shaman, Necromancer, Wizard, Magician, Enchanter})` |
| `IS_NOT_CLASS_BARD` | `GetClass() != Class::Bard` | `!HasClass(Class::Bard)` |

### 5.2 Directly verified correct (F-type evidence)

| Area | File:Lines | Status | Notes |
|------|-----------|--------|-------|
| `CheckDoubleAttack` / `CheckTripleAttack` | `zone/attack.cpp:4286-4313` | Verified correct | Uses `HasClass()` loop over owned classes; triple-attack best-of added in `17944113`. |
| `GetFactionLevel` iteration | `zone/client.cpp:9753-9765` | Verified correct | Iterates `GetClassesBits()`. |
| `GetSpellLevelForCaster` min-level aggregation | `zone/spell_effects.cpp:5906-5919` (decl `zone/mob.h:993`) | Verified correct | MIN across owned classes. Introduced in `zone/spells.cpp` by `70ad9f2c`; the definition was later relocated to `spell_effects.cpp`. |
| `CanCastSpell` union semantics | `zone/spells.cpp` | Verified correct | Union over owned classes. |
| `MaxSkill` / `CanHaveSkill` | `zone/skills.cpp` | Verified correct | Loops `HasClass()` over owned. |
| `CalcBaseHP` / `CalcBaseEndurance` | `zone/client.cpp` | Verified correct | MAX semantics over owned classes. |
| `Mob::GetClassesBits()` / `Mob::HasClass()` | `zone/mob.cpp:4842, 4855` | Verified correct | Core mask helpers. |
| `IsWarriorClass(uint8 class_id=0)` default path | `zone/mob.cpp:1180` | Verified correct | Uses `GetClassesBits()` when called with default 0. |
| `IS_CLASS_PURE_MELEE` | `spell_effects.cpp` | Verified correct | `HasClass(Monk) \|\| HasClass(Rogue) \|\| HasClass(Warrior) \|\| HasClass(Berserker)`. |
| Individual `IS_CLASS_WARRIOR`…`IS_CLASS_BERSERKER` (IDs 423-497) | `spell_effects.cpp:8423-8497` | Verified correct | All use `HasClass()`. |
| `IS_CLIENT_AND_MALE/FEMALE_BEASTLORD_BERSERKER_MONK_RANGER_ROGUE` | `spell_effects.cpp:9019-9023, 9035-9039` | Verified correct | Explicit `HasClass()` calls. |
| **NPC-only Bard attack-while-casting** | `zone/mob_ai.cpp:1376, 1428` | Verified correct (NPC path) | `IsCasting() && GetClass() != Class::Bard` — NPC AI (spell/flee interrupts), NPCs don't multiclass; correct rejection for non-Bard NPCs. |
| **NPC-only frontal stun immunity** | `zone/attack.cpp:4688` | Verified correct (NPC path) | See B-3 above. |
| **NPC-only default class bitmask** | `zone/special_attacks.cpp:2239` (in `NPC::DoClassAttacks`, 2231) | Verified correct (NPC path) | `class_bitmask` defaults to `GetPlayerClassBit(GetClass())` only for plain NPCs; the **Client** supplies swarm pets with the full `GetClassesBits()` at `client.cpp:8866`, and the Client override of DoClassAttacks is at `special_attacks.cpp:2416`. Not a multiclass path. |
| **Meditate pool selection** | `zone/client_mods.cpp:688` | Verified correct | `if (GetClass() != Class::Bard \|\| RuleB(Custom, MulticlassingEnabled))` — Bard gains meditation only when multiclassing is on; the `GetClass()` sentinel here is **correctly used as the Bard-sentinel test** (single-class fallback for non-multiclass servers). `HasSkill(SkillMeditate)` gate at `client_mods.cpp:662`. |
| **Merchant faction worst-of** | `GetModCharacterFactionLevel` | Fixed (`abc7826f`) | Worst-of over owned classes (must meet ANY faction check → the strictest owned class governs). |

### 5.3 Faction write/mutation path — verified class-insensitive (CLOSED — no findings; 2026-09-28)

End-to-end audit of the faction **mutation/write** path. **No multiclass write defect exists**; the
write path is class-insensitive by construction. Verified conclusions:

- **Stored-value invariant.** `faction_values.current_value` stores **raw accumulated personal faction
  relative to faction base** (a missing row and a stored `0` both mean "at base level"). Default range
  ±2000 (`common/features.h:208-209`), per-faction override via `faction_base_data`.
- **Sole persistence leaf.** `ZoneDatabase::SetCharacterFactionLevel()` (`zonedb.cpp:3555`) is the only
  writer of `faction_values` — `INSERT ... ON DUPLICATE KEY UPDATE` of the resulting **absolute** value.
- **Delta in logic, absolute in persistence.** Mutation functions add a delta to the loaded raw value,
  but the DB write stores the resulting absolute raw value; the in-memory `factionvalues` map is updated
  in the same call (`zonedb.cpp:3576`).
- **Single convergence point.** `SetFactionLevel()` (`client.cpp:9837`) and `SetFactionLevel2()`
  (`client.cpp:9922`) both route through `Client::UpdatePersonalFaction()` (`client.cpp:9991`), which
  applies Heroic CHA scaling, min/max clamp/repair, then writes.
- **All gameplay sources share the same raw path.** NPC kills (legacy `HateList::DoFactionHits`
  `hate_list.cpp:290`; merit path `attack.cpp:2812/2841/2871`), quests (`questmgr.cpp:1627, 1644`),
  tasks (`task_client_state.cpp:1038`), Perl/Lua APIs (`perl_client.cpp:388/393/398`,
  `lua_client.cpp:443/448`; content wrapper `lua_modules/client_ext.lua:1-4`), `RewardFaction()`
  (`client.cpp:10735`) and its faction-association fan-out (`client.cpp:10811`).
- **Class/race/deity are read-time modifiers only.** `CalculateFaction` (`common/faction.cpp:57`) adds
  `base + class_mod + race_mod + deity_mod` when computing *effective* standing; none of these are ever
  incorporated into the persisted raw value. `GetFactionData` (`zonedb.cpp:3461`) provides
  class-independent `min`/`max`/`base` plus class-keyed `class_mod` — the mutation clamps consume
  **only** `min`/`max`/`base`.
- **Heroic CHA scales hit magnitude** (`client.cpp:9996-10008`) but is stat-based and unrelated to
  multiclass class representation.
- **Bard sentinel is inert on the write path.** The sentinel can reach the mutation path — it flows
  through `GetClass()`/`GetBaseClass()` arguments (`SetFactionLevel2` re-sources `GetClass()` at
  `client.cpp:9936`; callers pass `GetBaseClass()`), but the resulting `class_mod` is **not consumed by
  the mutation calculation**, so it cannot change the delta, clamp, direction, or persisted value.
- **Default/per-faction min/max clamps are class-independent.** On-write `faction_minimum`/
  `faction_maximum` derive from `fm.min`/`fm.max`/`fm.base` only; no "can't improve/worsen beyond" logic
  consumes the class.
- **Bots/Mercs do not reach the Client faction writer.** Hate-list iteration filters `IsClient()`
  (`hate_list.cpp:298-301`), the raid loop skips `m.is_bot` (`attack.cpp:2806-2808`), the group loop
  filters `IsClient()`; no bot/merc faction writers exist.
- **`abc7826f` scope respected.** It changed the effective/read path only
  (`GetModCharacterFactionLevel`, worst-of-owned-class). It did not leave a corresponding mutation bug,
  because the mutation path never consumes class data.

**Preserved informational trap:** `SetFactionLevel2()` ignores its `char_class`, `char_race`, and
`char_deity` parameters, re-sourcing `GetClass(), GetFactionRace(), GetDeity()` (`client.cpp:9936`).
Behaviorally inert today; could matter if future code begins consuming class-specific modifiers during
mutation.

**Preserved informational content mirror:** `bazaar/Ambassador_Terratoe.pl:107-129` (`set_effective_factions`)
reverse-engineers a delta from the multiclass-aware read `GetModCharacterFactionLevel` and applies it to
the raw store via `SetFactionLevel2`, iterating up to 3 passes to absorb Heroic CHA scaling. **Not a
multiclass defect.** Robustness caveat: convergence is heuristic (MEDIUM confidence on exact convergence).

**Temporary-faction mechanics (reference):** `temp` is remapped 2→0 / 3→1 at write
(`zonedb.cpp:3560-3564`); `temp=1` rows are purged on zone entry (`RemoveTempFactions` `zonedb.cpp:3429`,
called at `client_packet.cpp:1402`); `SendFactionMessage` suppresses the message for `temp 1/2`
(`client.cpp:10191`). Class-independent; does not affect the multiclass conclusion.

---

## 6. Deferred, Policy-Blocked, and Out-of-Scope

### 6.1 B-1 — `DoDamageCaps` (FIXED — `9ba735e2`; MAX / best-of-owned class cap; LOW under shipped config)

`zone/attack.cpp:1380, 1398, 1416, 1434, 1452`. Five identical `switch (GetClass())` blocks; Bard
sentinel (8) is not in any `case`, so every multiclass-enabled **Client** fell through to the melee
`default` cap. The representation mismatch was real; its practical shipped scope was much smaller than
originally documented, and the then-current behavior was the globally most permissive tier.

**Reachability (shipped config).** The shipped rule `Combat:LevelToStopDamageCaps` = **20** (ruletypes
default; confirmed in the `release-peq.sql` DB snapshot in `database/release-peq.zip`). `DoDamageCaps`
returns uncapped above `stop_level` (`attack.cpp:1363-1365`, pre-fix), so the class-dependent tiers execute
**only at levels 1–19** on the shipped NMS configuration. This is **conditional** on the server's
runtime value: rules are DB-stored and a server can raise the rule (the optional SQL
`utils/sql/git/optional/2017_01_17_LevelStopDamageCaps.sql` documents "1 disables, 20 approximates old
behavior"). Tier bands 20–29 / 30–39 / 40–69 engage only if the server raises it; levels ≥70 use
class-independent level multipliers (`7*level` / `6*level` / `5*level` / `4*level`) and are never
class-split.

**Reachable class tiers (shipped config; levels `<10` / `10–19`):**

| Tier | `< 10` | `10–19` |
|------|--------|---------|
| `default` (melee: Warrior, Paladin, Ranger, Bard, Monk, Rogue, SK, Beastlord, Berserker) | 10 | 14 |
| Priest (Cleric, Druid, Shaman) | 9 | 12 |
| INT caster (Necromancer, Wizard, Magician, Enchanter) | 6 | 10 |

Non-reachable on shipped config (reference only): 20–29 → 30/20/12; 30–39 → 60/26/18; 40–69 →
200/80/40. The `default` tier is the **global highest** in every band (historical failure mode:
permissive, not fail-closed — the sentinel routed every multiclass-enabled Client into the
default/global-highest tier, `14`/`10` > priest `12`/`9` > caster `10`/`6`).

**Affected-set correction (post-fix).** A multiclass-enabled Client's cap changed **from** the
Bard-sentinel result **only when NONE of its owned classes belongs to the existing nine-class
default/melee tier**: Warrior, Paladin, Ranger, ShadowKnight, Monk, Bard, Rogue, Beastlord, Berserker.
Affected owned sets are therefore those consisting entirely of Cleric, Druid, Shaman, Necromancer,
Wizard, Magician, Enchanter — e.g. Wizard-only (multiclass mode), Cleric-only (multiclass mode),
Cleric+Druid, Cleric+Wizard, Shaman+Necromancer. **Cleric+Wizard is a representative example, not the
only affected combination.** Any owned set containing at least one of the nine default/melee-tier
classes continues to select the default/melee tier, because it is the highest of the three class tiers
below level 70.

**Implemented semantics (`9ba735e2`).** MAX / best-of-owned-class cap: the existing per-class/per-level
cap table is evaluated for every owned playable class and the **highest** cap is used. The function no
longer consults `GetClass()` (Bard sentinel) for tier selection. The implementation does **not** add
caps together, use MIN, use the Bard sentinel/default as the normal multiclass policy, change the cap
table, change `Combat:LevelToStopDamageCaps`, change any caller, change NPC/pet/merc reachability, or
redesign damage balance.

**Implementation shape.** The cap table moved verbatim into the file-scope helper
`static int DoDamageCapByClass(uint8 class_id, uint8 level)` (`attack.cpp:1363-1469`); it is pure
(class_id + level only). `Mob::DoDamageCaps()` (`attack.cpp:1471-1497`) preserves the
`LevelToStopDamageCaps` early return and the final `std::min((int64)cap, base_damage)`, captures
`GetClassesBits()` once, iterates `Class::Warrior` … `Class::Berserker` inclusively, tests membership
with `HasClass(i, classes_bits)`, and combines `std::max(cap, DoDamageCapByClass(i, level))`. A zero
playable-class mask (non-player entity) falls back to `DoDamageCapByClass(0, level)`, which reaches the
switch `default` tier and preserves the legacy `switch (GetClass())` default-tier result — defensive
only, unreachable on live paths (Clients always own ≥ 1 playable class; Bots carry one real bit).

**Cap table (preserved verbatim, unmodified):** level ≥ 125 → `7*level`; ≥ 110 → `6*level`; ≥ 90 →
`5*level`; ≥ 70 → `4*level`. 40–69 → priest 80 / caster 40 / default-melee 200; 30–39 → 26 / 18 / 60;
20–29 → 20 / 12 / 30; 10–19 → 12 / 10 / 14; <10 → 9 / 6 / 10 (retained comment
`// this is where the 20 damage cap comes from` on the default=10 branch). Class groups: priest =
{Cleric, Druid, Shaman}; caster = {Necromancer, Wizard, Magician, Enchanter}; default/melee = all
other playable classes.

**Preservation by entity type:**

- Multiclass-**disabled** single-class Client: unchanged (`GetClass()` is real; `GetClassesBits()`
  returns its single class bit).
- Bot with a normal player class: unchanged (real single-class bit).
- Multiclass-enabled Client with **only** priest/caster class(es): changed intentionally from the
  Bard/default sentinel tier to the actually owned class tier (this is the fix).
- Multiclass-enabled Client owning a default/melee-tier class: cap equivalent to the prior
  Bard/default-tier result.
- NPC / pet / merc: reachability unchanged by this commit; they gain no new `DoDamageCaps` path.
- Zero playable-class mask: defensive fallback preserves the legacy default tier.

**Caller asymmetry (adjacent, upstream-consistent — not B-1 multiclass defects).** Ordinary melee
(`Mob::Attack`, `attack.cpp:1695`) caps **`base_damage`** only; Client-only archery
(`CommonOutgoingHitSuccess`, `attack.cpp:6853-6871`, `IsClient()`-gated) caps the post-scaling
**`damage_done`**; **throwing is not handled by this function**; NPC/pet/Bot ranged is uncapped (not a
Client path). Unchanged by `9ba735e2`.

**Decision record — semantic categories (established by this audit):**

- Player-facing numeric class-derived **magnitude**: best / MAX of owned classes.
- Binary class **eligibility** (disciplines, AAs, spells, proc conditions, class limits): union / any
  owned class.
- Observer-facing identity / requirement **gate**: worst / MIN where appropriate (merchant-faction
  worst-of precedent, `abc7826f`).

MAX precedents investigated: `GetACSoftcap` / `GetSoftcapReturns`, `Client::MaxSkill`,
`CalcBaseHP` / `CalcBaseEndurance` / `CalcBaseMana`, `GetClassRaceACBonus`, `CheckTripleAttack`
(`17944113`), `GetDamageTable` / best-table behavior. **MAX is not universal** — the audit established a
context-dependent rule.

**Policy options considered (historical — pre-decision; MAX was selected and implemented):**

| Policy | Description | Precedent / category | Outcome |
|--------|-------------|----------------------|---------|
| Status quo (sentinel) | Every multiclass-enabled Client gets the `default` / global-highest cap; Client class tiers are effectively disabled | Was current live behavior; requires no code. Permissive by construction. | **Rejected** — leaves Client class tiers disabled |
| MIN (most restrictive) | Tightest cap among owned classes | `GetFactionLevel` / `GetModCharacterFactionLevel` worst-of; `GetSpellLevelForCaster` min-level — requirement-ordering precedents, not cap-table precedents | **Rejected** — conflicts with cap/progression precedents |
| MAX (least restrictive) | Highest cap among owned classes | Cap/progression precedent: `MaxSkill`, `CalcBaseHP`/`CalcBaseEndurance`, `Mob::GetACSoftcap`/`GetSoftcapReturns`, `CheckTripleAttack` (`17944113`). Design convention, not a mechanically forced result | **SELECTED and IMPLEMENTED — `9ba735e2`** |
| Melee wins | Any `default`-group class owned → melee cap | `Mob::Attack:1721` `IsWarriorClass()` melee damage-bonus gate. Coincides with MAX except priest/caster-only ownership | **Rejected** — MAX-of-owned is the established magnitude convention |

MAX-of-owned and melee-wins **coincide** except for priest/caster-only ownership. **Gameplay-balance
note (historical reasoning behind the selection):** owned-class semantics is **not** a
representation-only repair. Because sentinel behavior was the global-highest tier, MAX-of-owned
*lowers* low-level caps for caster/priest-only owned sets (e.g., pure Wizard 14→10 at 10–19; pure
Cleric 14→12); MIN-of-owned would lower them broadly. The choice of MAX was a deliberate
gameplay-balance decision consistent with the established magnitude convention.

### 6.2 B-6 — `MerchantRejectMessage` rejection text (OPEN — policy/design deferred; cosmetic-only; MEDIUM)

Verified 2026-09-28. `Client::MerchantRejectMessage()` is `zone/client.cpp:10093-10160`. Its sole caller
is `Client::Handle_OP_ShopRequest()` (`zone/client_packet.cpp:14477`), and it executes **only after** the
merchant-open faction gate has already rejected the client (`client_packet.cpp:14472-14479`,
`factionlvl >= 7`). It has **no effect** on merchant access, buying, selling, faction values, or
transaction state; its only effect is selecting and emitting a rejection `SayString`.

**The gates are already multiclass-aware:**

- The merchant-open gate uses `Client::GetFactionLevel()` (`client.cpp:9747`, worst-of-owned-class
  `std::min` loop), so rejection/acceptance is already worst-class correct.
- Item-level merchant faction checks (AdventureMerchant; AltCurrency merchant request/purchase/sell —
  `client_packet.cpp:2222, 2412, 2796, 2858, 3065, 3192`) use the `abc7826f` fix to
  `GetModCharacterFactionLevel()` (`client.cpp:10044`).

**The two remaining defects are message-only:**

1. **Reason attribution.** `MerchantRejectMessage()` independently calls
   `content_db.GetFactionData(&fmod, GetClass(), GetFactionRace(), GetDeity(), primaryfaction)`
   (`client.cpp:10102`). Under multiclassing `GetClass()` returns the Bard sentinel
   (`client.cpp:1990`), so it uses **Bard's** `class_mod` when choosing whether to blame
   deeds / race / class / deity.
2. **Displayed class.** If the class branch is selected,
   `merchant->SayString(zone->random.Int(WONT_SELL_CLASS1, WONT_SELL_CLASS5), itoa(GetClass()))`
   (`client.cpp:10152`, `%B3(13)` class-name field) renders **"Bards"** even when the client
   **does not own** Bard.

**Cannot permit or deny a transaction.** The allow/deny decision is made solely by the caller's
worst-of-class gate; this function is void and mutates no merchant/transaction state. With
multiclassing disabled, behavior is correct/upstream-equivalent. Bots/Mercs do not reach this
Client-only path.

**Design constraint.** There is no established NMS concept of a single primary/representative
multiclass class. `GetModCharacterFactionLevel()` returns only the numeric worst value and does not
expose the owned class responsible for the worst class modifier — displaying that class would require
additional argmin tracking/API logic. The audit established the **gate** semantics (worst-of-class),
**not** a UI/message semantics; no "worst faction class" message meaning is implied.

**Viable product/UI choices (decide when/if touched):** display the worst-modifier owned class;
enumerate owned classes (client-label precedent, `MQ2Labels.cpp:924-946`); use class-neutral rejection
wording; or intentionally retain the legacy/sentinel behavior. Choosing among them is a product/UI
decision, not a mechanical fix.

**Classification: cosmetic-only multiclass defect; policy/design deferred.**
Confidence **HIGH**: merchant access/transactions unaffected. Confidence **HIGH**: Bard-sentinel
attribution/display is wrong for clients not owning Bard.

### 6.3 C-4 plate-user Jann/illusion restrictions (FIXED — `2ce84053`)

Former open finding at `zone/spell_effects.cpp:9009-9031`. Affected restriction IDs: **11044, 11090,
11210, 11211** (male/female plate-user; male/female Druid-Enc-Mag-Nec-Shm-Wiz).

Preserved findings:

- **Class sets.** Plate (`IsPlateClass`): `{Warrior, Cleric, Paladin, ShadowKnight, Bard}`. Caster-without-Cleric
  (the old `IsCasterClass(GetClass()) && GetClass() != Class::Cleric`): `{Druid, Shaman, Necromancer, Wizard, Magician, Enchanter}`.
  The three Jann groups (plate 5 + caster 6 + melee 5) form the complete **16-class partition** per gender.
- **Old Bard-sentinel behavior.** `GetClass()` = Bard (8): plate cases evaluated `IsPlateClass(8)=true` →
  **fail-open** (every client passed the plate gate); caster cases evaluated `IsCasterClass(8)=false` →
  **fail-closed** (Jann caster illusion always blocked for multiclass clients).
- **Melee sibling cases** `IS_CLIENT_AND_MALE/FEMALE_BEASTLORD_BERSERKER_MONK_RANGER_ROGUE`
  (9019/9035) were **already mask-aware** (`HasClass()`) and correct — the precedent for this fix.
- **Dormancy.** The four restriction IDs were **dormant in shipped spell data** at audit time (no
  `CastRestriction`, `caster_requirement_id`, or SPA-442/443 use). Dormancy meant *latent*, not *correct*;
  the fix removes the latent representation bug.
- **Fix.** `2ce84053` converts both sexes to `HasAnyClass(...)` union/any-owned semantics while preserving
  the `IsClient() && gender` gates, `return true; break;`, and per-case independence (no case merging).
- **Non-Client behavior unchanged.** `IsClient()` remains the first conjunct, so NPC/Bot/Merc targets still
  short-circuit and fail the requirement exactly as before.

The message-mirror (second switch) at `spell_effects.cpp:10227-10244` prints the rejection strings for the
same IDs and has no class logic — unaffected by this finding.

### 6.4 END-OR-MANA — DEFERRED (confirmed latent representation bug; policy unresolved)

Status wording (authoritative): **Deferred — confirmed latent representation bug; policy unresolved; no
shipped spell-data use found.**

`zone/spell_effects.cpp:9077 (ABOVE_20), 9086 (BELOW_10), 9098-9099 (BELOW_30/_2)`. Each selects an
endurance branch when `IsNonSpellFighterClass(GetClass())` (or `IsHybridClass` in BELOW_10).

Accumulated findings:

1. **Bard-sentinel branch selection is broken under multiclassing.** `GetClass()` = 8 → the helpers all
   evaluate the **Bard** class, so the branch choice (endurance vs. mana) is decided by Bard membership,
   not by the character's actual classes. A multiclass Necromancer+Warrior would take the endurance path
   because Bard is a non-spell-fighter — exactly backwards.
2. **No shipped spell data uses these proc IDs.** Searches found zero tiles/spells in the shipped
   content that reference these conditions, so the defect is **latent**: it cannot currently manifest in
   shipped gameplay.
3. **Policy unresolved.** The multiclass resource-policy question (union/intersection — is a
   fighter+caster “non-spell-fighter”? does ANY owned class win?) mirrors C-3/C-4 semantics and was never
   decided. Reasonable resolution is `HasAnyClass(non-spell-fighter)` for the endurance branch, but it
   requires its own decision. **B-1's closure (`9ba735e2`, damage-cap MAX-of-owned) does not decide this**
   — damage-cap magnitude and END-OR-MANA branch selection are separate questions.
4. **Decision: do not modify dormant behavior.** Because no shipped data exercises these branches, any
   change ships untested policy. The END-OR-MANA do-not-modify disposition stands (§9 item 1) and is
   not affected by B-1 closure.
5. **Separate observation — unguarded `CastToClient()`.** These three cases call
   `CastToClient()->GetEndurancePercent()` without an `IsClient()` guard in the switch. In practice the
   proc-condition switch only runs for client targets, so this is a latent safety nit, not a live crash;
   worth hardening when the cases are eventually fixed.
6. **Separate observation — missing message ID 49545.** No `GetProcStrings`/message string is mapped for
   proc string `49545` (observed during the END-OR-MANA investigation); unrelated to the class bug but
   noted for completeness.

### 6.5 Other C-4 remnants (historical)

The old audit’s C-4 spreadsheet row also referenced `IS_END_OR_MANA_*` and the plate-user set (now fixed
by `2ce84053`, §4/§6.3); the big `calc` rows for `IS_CLASS_KNIGHT_HYBRID_MELEE`,
`IS_CLASS_WARRIOR_CASTER_PRIEST`, and `IS_CLASS_CASTER_PRIEST` were fixed by `b1aa4b5d`. See §4 matrix
for the live split.

### 6.6 Out of scope (deliberately not audited for fixes)

| Area | Reason |
|------|--------|
| Bots (`bot.cpp`, all `GetClass()`; D-1..D-4) | Bots do not multiclass; bot subsystem deferred. |
| NPCs / mercenaries | Don't multiclass. |
| `aggro.cpp:1515` `DoubleAttackChance` | Skill-based, not class-based. |
| Quest scripting (`questmgr.cpp`, Perl/Lua API) | Audited separately; documented in AGENTS.md (Perl `GetClassBitmask` vs `GetClassesBitmask` inconsistency is a scripting trap, not a core finding). |
| Inventory/SwapItem | Fixed (`17944113`); see §3. |

---

## 7. Audit Traps, Representation Pitfalls, and Documentation Corrections

### 7.1 Traps (verified pitfalls repeated across audits)

- **`GetClass()` is the Bard sentinel in all Client gameplay paths when multiclassing is on.** Treat any
  reading of it as suspect; prefer `HasClass()` / `GetClassesBits()`.
- **Class ID ≠ class bitmask.** `GetPlayerClassBit(12)` = 2048, not 12. Passing a raw ID where a mask is
  expected (or vice versa) is a bug. Same truth in reverse: `player_class_bitmasks[]` values
  (`1u << (class_id - 1)`) are not class IDs.
- **AA ability masks are double-encoded.** DB (item-style) → loaded with `<< 1` (client-style). The gate
  must use `(ability->classes >> 1)`. (See §4.1.)
- **`PassLimitClass` takes a class ID, not a mask**, for the class argument; its first argument is a
  spell-bitmask (Bard=256).
- **The default-arg wrappers are safe; the explicit-arg form is not.** `IsWarriorClass()` vs
  `IsWarriorClass(GetClass())`.
- **Unreachable else-branches look like dead bugs.** Under multiclassing the `else` (single-class) paths
  of `SendAlternateAdvancementRank` and `CanUseAlternateAdvancementRank` (aa.cpp:1019, 1960) are correct
  single-class code, not latent multiclass bugs. Trace reachability before "fixing".
- **`spells[].classes[]` is indexed by `class_id - 1`.** Index **7 is the Bard column**, index **13 the
  Enchanter column** (Bard ID 8, Enchanter ID 14). A lookup keyed off the Bard sentinel `GetClass()`
  reads the Bard column — not any owned class — so resolve via `GetSpellLevelForCaster()`
  (earliest/minimum level) instead of a sentinel-indexed column read.

### 7.2 Documentation corrections vs. prior audit files

- The original foundation audit attributed several fixes to the wrong commits. This document corrects
  them (Ledger, §3): `17944113` (inventory + triple attack), `abc7826f` (faction), `63d1a850`
  (disciplines), `70ad9f2c` (scribe + `GetSpellLevelForCaster`), `e008d038` (class-gate removal),
  `2690ef5f` (attack-while-casting), `a2dc4ac3` (SE_LimitClass), `92cfd33c` + `7d328cf4` (Berserker),
  `974cb171` + `b1aa4b5d` (PassCastRestriction batches).
- **B-5 is closed as already-correct** in §4.1; the original doc listed it as an open HIGH fix.
- **B-3 is reclassified** NPC-only (§4).
- **A-1/A-2/A-3** are the three SE_LimitClass sites, all fixed by `a2dc4ac3` (the original doc only
  itemized one of the three; the fix commit confirms three hunks).
- Prior docs **deprecated** by this canonical record: `nms-multiclass-audit.md`,
  `audit_bard_attack_while_casting.md`, `audit_bard_attack_while_casting_RECHECK.md`. They are retained
  unmodified for history (banner added only) and must not be re-edited as live findings.
- **`GetSpellLevelForCaster` location.** §5.2 previously pinned it to `zone/spells.cpp`; the current
  definition is `zone/spell_effects.cpp:5906-5919` (introduced by `70ad9f2c` in `spells.cpp`, later
  relocated). Use the location in §5.2 going forward.

---

## 8. Reconciliation Notes

- The opening count in §1 (36 this session + 164 carried; A=3/B=6/C=4/D=14/E=9/F=12) is the historical
  tally. The live status in §4 supersedes it. Counting the A/B/C rows listed in the §4 matrix (16 rows;
  C-4 spans multiple rows): **12 fixed** (A-1/2/3, B-1, B-2, B-4, C-1/2/3, C-4 hybrid+clr-shm-dru, C-4
  knight-hybrid/warrior-caster-priest/caster-priest/not-bard, C-4 plate-user), **2 closed/reclassified
  as already correct** (B-3 NPC-only, B-5 display redundancy), **1 deferred/policy-blocked** (C-4
  END-OR-MANA), **1 still open design question** (B-6 `MerchantRejectMessage` — cosmetic-only, verified
  2026-09-28; merchant gates already multiclass-aware).
- Faction **write-path** review (2026-09-28): **zero new source findings** — the mutation path is
  verified class-insensitive (see §5.3). No new rows added to the §4 matrix; §9 queue row removed.
- B-1 reconciliation/history (2026-09-28, §6.1): the representation mismatch was real, but shipped-config
  reachability was levels 1–19 (`Combat:LevelToStopDamageCaps` = 20) and the then-current behavior was
  the globally most permissive tier. The CRITICAL label was reconsidered to **LOW under the shipped
  config** (see §6.1); the all-melee `< L70`-style scope claims exist only in the deprecated audit and
  were corrected here. B-1 was subsequently **implemented and fixed as MAX/best-of-owned in `9ba735e2`**
  (2026-09-28) — historical; the closure is recorded in §6.1.
- Ledger hashes were re-verified against `git log` on `nms-development` on 2026-09-28. Do not trust the
  commit columns in older audit files (several were wrong).
- Line numbers in this document are the current `nms-development` snapshot (2026-09-29) unless the §3
  column says "pre-fix". Shift-tolerant searches should be used before re-editing.
- Sweep-2 reconciliation (2026-09-29): the S-series is tracked in §4.2 (not the historical A/B/C
  matrix; §1/§4 tallies unchanged). **S-08 FIXED** by `f01b999a` (pushed) — all five `/who` class-filter
  predicates tested the Bard sentinel field instead of owned-class membership: zone
  `EntityList::ZoneWho` count + emit, world `ClientList::SendWhoAll` count + emit, and
  `ClientList::ConsoleSendWhoAll`. World-side filtering required new transport, so `uint32 classes_bits`
  was **appended** to `ServerClientList_Struct` (238 → 242 bytes, field at offset 238, all prior packed
  offsets unchanged); `Client::UpdateWho` publishes both `class_` (retained for legacy/display consumers)
  and `classes_bits`; `ClientListEntry` stores the mask. One shared zone lambda and one shared
  world/console `WhoHasClass` helper now each range-check the full-width class ID **before** narrowing,
  restoring ANY-owned / union membership; the `0xFFFFFFFF` / `0xFFFF` no-class-filter sentinels and all
  anon/admin predicates are unchanged, and `classes_bits == 0` matches no class-specific filter with no
  Bard/`GetClass()` fallback. Success-path `UpdateWho()` calls were added to `AddExtraClass` /
  `RemoveExtraClass` so world `/who` state cannot go stale after an online class change. Full Release build
  passed; **no live in-game test was performed**. **Console class filtering is fixed, console multiclass
  class display is not** — three adjacent findings remain **unresolved and outside `f01b999a`**: LFG
  class-mask/sentinel behavior (`1 << CLE->class_()` and `Buffer->Class_ = CLE->class_()` untouched),
  console display still sentinel-backed and may print **BRD**, and the missing `GestaltClasses` bucket
  fallback/migration was **not** added. **S-07 FIXED** by `a845bbfd` (pushed) — Triptych class-ID →
  class-bitmask conversion regression in `zone/guild_mgr.cpp`; `ItemData::IsEquipable` already expected
  a class bitmask while three callers passed numeric class IDs, so ANY-owned/union eligibility was
  lost; all now pass `GetClassesBits()` (762/874/1721; 787 was already correct — false positive).
  Not multiclass-only: the same representation error hit ordinary single-class clients too. Race
  semantics, `ItemData::IsEquipable` itself, and guild-bank permission enforcement are unchanged;
  impact is the guild-bank usable/display flag only. **S-06 FIXED** by `d40a68f2` (pushed) — inherited canonical
  EQEmu `MaxSkills()` broken by the Triptych multiclass conversion; missing accumulator, missing
  non-multiclass path, and `GetLevel()` → `Character:MaxLevel` all repaired, restoring current-level
  caps with MAX/best-of-owned in multiclass mode. **S-04/S-05 FIXED** by `9ca593f8` (pushed).
  **S-03 FIXED** by `036c2177` (pushed) — one-line trainer-class change. **S-02 CLOSED**
  verified-correct / false positive, **not attributed** to any source commit. S-01 (policy) and
  S-09..S-10 (backlog) remain open and are surfaced in §4.2 and §9. The Perl/Lua `GetSkillTrainLevel`
  wrappers and `SkillCaps::GetSkillTrainLevel` itself were **not** part of `036c2177` and remain as
  recorded in §4.2 and §9.

---

## 9. Current Remaining Foundation-Audit Queue

Ordered by (a) severity, (b) whether policy is decided.

| # | Item | Ref | Blocker | Policy status |
|---|------|-----|---------|---------------|
| 1 | END-OR-MANA latent representation bug | C-4 (MEDIUM) | Resource-policy undecided + no shipped data to validate against | **Blocked** — dormant; do-not-modify decision stands |
| 2 | `MerchantRejectMessage` rejection text | B-6 (MEDIUM) | Product/UI choice: worst-modifier class, enumerate owned, neutral text, or sentinel | Open design question — cosmetic, non-gameplay |
| 3 | Bots (full subsystem) | D series / out-of-scope | Bot subsystem deferred by project scope | Out of scope until project says otherwise |
| 4 | Resist final bonuses combination rule (sentinel-gated `CalcFR/DR/PR/CR`) | S-01 | MAX vs SUM policy undecided | Open design question — stat-only, moderate |
| 5 | S-03 residuals — scripting `GetSkillTrainLevel` wrappers still pass the Bard sentinel (`perl_client.cpp:3416-3418`, `lua_client.cpp:3518-3521`); `SkillCaps::GetSkillTrainLevel` exact-level-row behavior unrepaired | S-03 follow-up | No trainer-class context in the scripting API; changing the helper would alter upstream cap semantics | Open — deliberately not changed by `036c2177` |
| 6 | Sweep-2 backlog — `/setstartcity`, SK/Pal ability refresh | S-09..S-10 | None | Not yet resolved |

**Hard stop conditions:** Do not modify dormant END-OR-MANA behavior; it is **not** made actionable by
the B-1 fix (`9ba735e2`) — the END-OR-MANA do-not-modify disposition is independent of B-1 and stands.
No source edits from this document are permitted until explicitly requested.

---

*End of NMS Multiclass Foundation Audit — Canonical Record*
*Canonical file: `nms-multiclass-foundation-audit.md`*
*Deprecated (retained for history): `nms-multiclass-audit.md`, `audit_bard_attack_while_casting.md`, `audit_bard_attack_while_casting_RECHECK.md`*