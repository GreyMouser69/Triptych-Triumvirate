# Audit: Bard Class Check in Mob::Attack() at attack.cpp:1622

> ## DEPRECATED — SUPERSEDED BY `nms-multiclass-foundation-audit.md`
>
> This document is retained **for history only**. The finding it records was fixed by commit `2690ef5f`
> (`attack.cpp:1622` now uses `!HasClass(Class::Bard)`), and its analysis is superseded by the canonical
> `nms-multiclass-foundation-audit.md`. Do not re-edit this file as a live findings ledger.

## Executive Summary

The condition GetClass() != Class::Bard at attack.cpp:1622 is a Category B (representation bug) when multiclassing is enabled. It incorrectly uses GetClass() (returns base class) instead of HasClass(Class::Bard) (checks owned classes bitmask).

Impact: Multiclass characters who own Bard as a secondary class are incorrectly blocked from attacking while casting, because GetClass() returns their base class, not Bard.

## 1. Surrounding Logic Analysis

Location: attack.cpp:1622 in Mob::Attack()
Behavior breakdown:
- IsCasting(): true when casting_spell_id != 0
- GetClass() != Class::Bard: checks if base class is NOT Bard (value 8)
- !IsFromSpell: false when attack originates from a spell effect (e.g., Rampage AA)
- Result: Attack is cancelled if character is casting AND not a Bard AND not a spell-triggered attack
- Comment: Only bards can attack while casting

Applicability:
- Called from Mob::Attack() - entry point for all melee attacks
- Also called from NPC::Attack() and Client::DoAttackRounds()
- Applies to Clients, NPCs, Bots, Pets (any Mob)
---
### Multiclass Behavior Summary
| Scenario | GetClass() | GetClassesBits() | HasClass(Bard) |
|----------|--------------|--------------------|------------------|
| Single-class Bard | Bard (8) | 128 (bit 7) | true |
| Single-class Warrior | Warrior (1) | 1 | false |
| Multiclass Warrior+Bard | Warrior (1) | 129 (1+128) | true |
| Multiclass Wizard+Bard | Wizard (12) | 128+2048=2176 | true |
---
---

## 3. Historical Purpose of Bard Exception

Comment at line 1629: // Only bards can attack while casting

Origin: This is legacy EQ behavior. Bards have a unique mechanic where they can maintain songs while auto-attacking. The exception allows Bards to continue auto-attacking while their song casting timer is active.

Comment at line 1605: // IsFromSpell added to allow spell effects to use Attack. (Mainly for the Rampage AA right now.)

IsFromSpell bypasses the Bard check for spell-triggered attacks (e.g., Rampage AA procs).
---
## 4. Behavior Matrix

| Character Config | Multiclass Enabled? | GetClass() | GetClass() != Bard | HasClass(Bard) | Attack While Casting? |
|------------------|---------------------|--------------|----------------------|------------------|------------------------|
| Single-class Bard | N/A | Bard (8) | FALSE | true | ALLOWED |
| Single-class Warrior | N/A | Warrior (1) | TRUE | false | BLOCKED |
| Multiclass enabled | | | | | |
| Warrior only | Yes | Warrior (1) | TRUE | false | BLOCKED |
| Warrior + Bard | Yes | Warrior (1) | TRUE | true | BLOCKED (BUG) |
| Wizard + Bard | Yes | Wizard (12) | TRUE | true | BLOCKED (BUG) |
| Cleric + Wizard | Yes | Cleric (2) | TRUE | false | BLOCKED |
| Bard + Cleric | Yes | Bard (8) | FALSE | true | ALLOWED |

Critical finding: A multiclass character whose base class is not Bard but who owns Bard as a secondary class is incorrectly blocked from attacking while casting.
---
## 5. Correct Multiclass Semantic Determination

### Options Evaluated

| Option | Logic | Evidence |
|--------|-------|----------|
| A. Union semantics | !HasClass(Class::Bard) -- any owned Bard grants exception | Consistent with MaxSkill(), CalcBaseHP(), CheckTripleAttack(), CanHaveSkill() -- all use union semantics |
| B. Primary class only | GetBaseClass() == Bard | No precedent; GetBaseClass() only used for faction/display |
| C. Spell-source | Check if current cast is Bard song | No precedent; IsFromSpell already handles spell-procs |
| D. Current class | Requires active class concept | No active-class concept exists in NMS |
---

### Evidence for Option A (Union Semantics)

| System | Multiclass Behavior | Code Reference |
|--------|---------------------|----------------|
| HP/Endurance | MAX across owned classes | CalcBaseHP(), CalcBaseEndurance() |
| Skill Caps | MAX across owned classes | MaxSkill() loops HasClass(i) |
| Triple Attack | MAX chance across owned classes | CheckTripleAttack() line 4286-4313 |
| Spell Eligibility | UNION (any class) | CanHaveSkill() line 4262 |
| Spell Level | MIN level across classes | GetSpellLevelForCaster() line 5894 |
| Faction | MIN (worst) | GetFactionLevel() line 9753-9765 |
| Item Equip | UNION | IsEquipable() uses bitmask |
---

Conclusion: The established pattern for beneficial/permissive mechanics (attack while casting is a permission) is UNION -- any qualifying class grants the benefit.
---
## Verification Plan

1. Compile and run unit test: multiclass Bard+Warrior attempts attack while casting -> should succeed
2. Verify single-class Warrior still blocked
3. Verify single-class Bard still allowed
3. Verify spell-triggered attacks (IsFromSpell) still bypass check
4. Run full regression test suite
## Verification Plan

1. Compile and run unit test: multiclass Bard+Warrior attempts attack while casting -> should succeed
2. Verify single-class Warrior still blocked
3. Verify single-class Bard still allowed
3. Verify spell-triggered attacks (IsFromSpell) still bypass check
4. Run full regression test suite
## Verification Plan

1. Compile and run unit test: multiclass Bard+Warrior attempts attack while casting -> should succeed
2. Verify single-class Warrior still blocked
3. Verify single-class Bard still allowed
3. Verify spell-triggered attacks (IsFromSpell) still bypass check
4. Run full regression test suite
## Verification Plan

1. Compile and run unit test: multiclass Bard+Warrior attempts attack while casting -> should succeed
2. Verify single-class Warrior still blocked
3. Verify single-class Bard still allowed
3. Verify spell-triggered attacks (IsFromSpell) still bypass check
4. Run full regression test suite
## Verification Plan

1. Compile and run unit test: multiclass Bard+Warrior attempts attack while casting -> should succeed
2. Verify single-class Warrior still blocked
3. Verify single-class Bard still allowed
3. Verify spell-triggered attacks (IsFromSpell) still bypass check
4. Run full regression test suite
## Verification Plan

1. Compile and run unit test: multiclass Bard+Warrior attempts attack while casting -> should succeed
2. Verify single-class Warrior still blocked
3. Verify single-class Bard still allowed
3. Verify spell-triggered attacks (IsFromSpell) still bypass check
4. Run full regression test suite
## 6. Unintended Consequences of Fix

Proposed fix: Replace GetClass() != Class::Bard with !HasClass(Class::Bard)

Impact analysis:

| Character | Current Behavior | After Fix |
|-----------|------------------|-----------|
| Single-class Bard | Allowed | Allowed |
| Single-class non-Bard | Blocked | Blocked |
| Multiclass Bard+X | Blocked (bug) | Allowed |
| Multiclass non-Bard | Blocked | Blocked |
| Bard only (MC disabled) | Allowed | Allowed |
---

No unintended side effects -- the fix only unblocks valid multiclass Bards who were incorrectly blocked.
---
## 7. Final Classification

| Finding | Classification | Severity |
|---------|----------------|----------|
| GetClass() != Class::Bard at attack.cpp:1622 | B -- Representation bug (uses class ID instead of bitmask) | HIGH -- Blocks valid multiclass Bard gameplay |
---
## Verification Plan

1. Compile and run unit test: multiclass Bard+Warrior attempts attack while casting -> should succeed
2. Verify single-class Warrior still blocked
3. Verify single-class Bard still allowed
3. Verify spell-triggered attacks (IsFromSpell) still bypass check
4. Run full regression test suite
## Verification Plan

1. Compile and run unit test: multiclass Bard+Warrior attempts attack while casting -> should succeed
2. Verify single-class Warrior still blocked
3. Verify single-class Bard still allowed
3. Verify spell-triggered attacks (IsFromSpell) still bypass check
4. Run full regression test suite
## Verification Plan

1. Compile and run unit test: multiclass Bard+Warrior attempts attack while casting -> should succeed
2. Verify single-class Warrior still blocked
3. Verify single-class Bard still allowed
3. Verify spell-triggered attacks (IsFromSpell) still bypass check
4. Run full regression test suite
## Verification Plan

1. Compile and run unit test: multiclass Bard+Warrior attempts attack while casting -> should succeed
2. Verify single-class Warrior still blocked
3. Verify single-class Bard still allowed
3. Verify spell-triggered attacks (IsFromSpell) still bypass check
4. Run full regression test suite
## Verification Plan

1. Compile and run unit test: multiclass Bard+Warrior attempts attack while casting -> should succeed
2. Verify single-class Warrior still blocked
3. Verify single-class Bard still allowed
3. Verify spell-triggered attacks (IsFromSpell) still bypass check
4. Run full regression test suite
## Verification Plan

1. Compile and run unit test: multiclass Bard+Warrior attempts attack while casting -> should succeed
2. Verify single-class Warrior still blocked
3. Verify single-class Bard still allowed
3. Verify spell-triggered attacks (IsFromSpell) still bypass check
4. Run full regression test suite
## Verification Plan

1. Compile and run unit test: multiclass Bard+Warrior attempts attack while casting -> should succeed
2. Verify single-class Warrior still blocked
3. Verify single-class Bard still allowed
3. Verify spell-triggered attacks (IsFromSpell) still bypass check
4. Run full regression test suite
## Verification Plan

1. Compile and run unit test: multiclass Bard+Warrior attempts attack while casting -> should succeed
2. Verify single-class Warrior still blocked
3. Verify single-class Bard still allowed
3. Verify spell-triggered attacks (IsFromSpell) still bypass check
4. Run full regression test suite
## Verification Plan

1. Compile and run unit test: multiclass Bard+Warrior attempts attack while casting -> should succeed
2. Verify single-class Warrior still blocked
3. Verify single-class Bard still allowed
3. Verify spell-triggered attacks (IsFromSpell) still bypass check
4. Run full regression test suite
## Verification Plan

1. Compile and run unit test: multiclass Bard+Warrior attempts attack while casting -> should succeed
2. Verify single-class Warrior still blocked
3. Verify single-class Bard still allowed
3. Verify spell-triggered attacks (IsFromSpell) still bypass check
4. Run full regression test suite
## Verification Plan

1. Compile and run unit test: multiclass Bard+Warrior attempts attack while casting -> should succeed
2. Verify single-class Warrior still blocked
3. Verify single-class Bard still allowed
3. Verify spell-triggered attacks (IsFromSpell) still bypass check
4. Run full regression test suite
## Verification Plan

1. Compile and run unit test: multiclass Bard+Warrior attempts attack while casting -> should succeed
2. Verify single-class Warrior still blocked
3. Verify single-class Bard still allowed
3. Verify spell-triggered attacks (IsFromSpell) still bypass check
4. Run full regression test suite
## Verification Plan

1. Compile and run unit test: multiclass Bard+Warrior attempts attack while casting -> should succeed
2. Verify single-class Warrior still blocked
3. Verify single-class Bard still allowed
3. Verify spell-triggered attacks (IsFromSpell) still bypass check
4. Run full regression test suite
