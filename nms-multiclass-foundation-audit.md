# NMS Multiclass Foundation Audit — Canonical Record

**Repository:** Triptych Triumvirate / NMS (EQEmu 23.8.1 fork, RoF2 client)
**Branch:** `nms-development`
**Compiled by:** opencode agent synthesis (prior explore-agent passes + direct file verification)
**Last updated:** 2026-09-28 (post-B-5 resolution; C-4 plate-user fixed in `2ce84053`; B-6 `MerchantRejectMessage` verified cosmetic-only — see §6.2; faction write-path verified class-insensitive — see §5.3; supersedes prior audit docs)

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
> formally CLOSED/RECLASSIFIED (B-3, B-5). The count table is the historical opening balance; §4 is the
> live status.

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
| Spell required level | **Minimum** qualifying class level |
| AA eligibility | Multiclass-aware (owned classes) |
| Faction stored value | Raw accumulated personal faction relative to base; class/race/deity modifiers applied at effective-faction read time only |

Exceptions that remain unresolved by policy are flagged in §6 (DoDamageCaps, END-OR-MANA proc
conditions).

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

Combined, these commits close **every A/B-2/B-4/B-5, all C-1/C-2/C-3**, the **cast-restriction C-4**
occurrences, and the **plate-user C-4 (Jann/illusion) set** (`2ce84053`). Remaining open items are in
§6 and §9.

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
| B-1 (CRITICAL) | `attack.cpp:1380-1452` `DoDamageCaps` switch over `GetClass()` — all multiclass chars fall through to the melee `default` cap | **DEFERRED — policy-blocked** | Semantics (MIN/MAX/melee-wins) not decided; see §6.1. No source fix. |
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
| `GetSpellLevelForCaster` min-level aggregation | `zone/spells.cpp` | Verified correct | MIN across owned classes (added in `70ad9f2c`). |
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

### 6.1 B-1 — `DoDamageCaps` (DEFERRED — policy-blocked; CRITICAL)

`zone/attack.cpp:1380, 1398, 1416, 1434, 1452`. Five identical `switch (GetClass())` blocks; Bard
sentinel not in any `case`, so all multiclass characters receive the melee `default` cap. **No fix made
because no semantic policy is set:**

| Policy | Description | Precedent |
|--------|-------------|-----------|
| MIN (most restrictive) | Tightest cap among owned classes | `GetFactionLevel` (min faction) |
| MAX (least restrictive) | Highest cap among owned classes | `CalcBaseHP`, `MaxSkill` |
| Melee wins | Any melee class owned → melee cap | No direct precedent |

Requires a project decision before any mechanical fix. Remains the top deferred item.

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
   must be decided alongside B-1's MIN/MAX policy.
4. **Decision: do not modify dormant behavior.** Because no shipped data exercises these branches, any
   change ships untested policy. Left as-is pending the §9 policy decision.
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

---

## 8. Reconciliation Notes

- The opening count in §1 (36 this session + 164 carried; A=3/B=6/C=4/D=14/E=9/F=12) is the historical
  tally. The live status in §4 supersedes it. Counting the A/B/C rows listed in the §4 matrix (16 rows;
  C-4 spans multiple rows): **11 fixed** (A-1/2/3, B-2, B-4, C-1/2/3, C-4 hybrid+clr-shm-dru, C-4
  knight-hybrid/warrior-caster-priest/caster-priest/not-bard, C-4 plate-user), **2 closed/reclassified
  as already correct** (B-3 NPC-only, B-5 display redundancy), **2 deferred/policy-blocked** (B-1
  `DoDamageCaps`, C-4 END-OR-MANA), **1 still open design question** (B-6 `MerchantRejectMessage` —
  cosmetic-only, verified 2026-09-28; merchant gates already multiclass-aware).
- Faction **write-path** review (2026-09-28): **zero new source findings** — the mutation path is
  verified class-insensitive (see §5.3). No new rows added to the §4 matrix; §9 queue row removed.
- Ledger hashes were re-verified against `git log` on `nms-development` on 2026-09-28. Do not trust the
  commit columns in older audit files (several were wrong).
- Line numbers in this document are the current `nms-development` snapshot (2026-09-28) unless the §3
  column says "pre-fix". Shift-tolerant searches should be used before re-editing.

---

## 9. Current Remaining Foundation-Audit Queue

Ordered by (a) severity, (b) whether policy is decided.

| # | Item | Ref | Blocker | Policy status |
|---|------|-----|---------|---------------|
| 1 | `DoDamageCaps` switch — multiclass chars get melee cap | B-1 (CRITICAL) | MIN/MAX/melee-wins choice | **Blocked** — no decision |
| 2 | END-OR-MANA latent representation bug | C-4 (MEDIUM) | Policy + no shipped data to validate against | **Blocked** — dormant; do-not-modify decision stands |
| 3 | `MerchantRejectMessage` rejection text | B-6 (MEDIUM) | Product/UI choice: worst-modifier class, enumerate owned, neutral text, or sentinel | Open design question — cosmetic, non-gameplay |
| 4 | Bots (full subsystem) | D series / out-of-scope | Bot subsystem deferred by project scope | Out of scope until project says otherwise |

**Hard stop conditions:** Do not begin item 2 until the B-1 policy decision is made. Do not modify
dormant END-OR-MANA behavior. No source edits from this document are permitted until explicitly requested.

---

*End of NMS Multiclass Foundation Audit — Canonical Record*
*Canonical file: `nms-multiclass-foundation-audit.md`*
*Deprecated (retained for history): `nms-multiclass-audit.md`, `audit_bard_attack_while_casting.md`, `audit_bard_attack_while_casting_RECHECK.md`*