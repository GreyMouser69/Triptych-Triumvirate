#!/bin/sh
# Phase A personal-loot gate invariants.
#
# Static, read-only check. No DB access, no game client, no build step.
# Verifies only the four Phase A gating invariants:
#
#   1. Custom:UsePersonalLoot is declared in ruletypes.h with default false
#   2. global_npc.pl reads the rule via quest::get_rule(...) eq "true"
#   3. inside nms_share_corpse_loot the $killer->GetRaid() bypass precedes
#      the rule gate
#   4. no active legacy activation gate remains on sharedloot / sharedloot_self
#
# sharedloot_maxitems is intentionally NOT prohibited: it is a per-corpse
# item cap, not an activation gate.
#
# Usage:  sh check-phase-a-loot-gate.sh          (run from repository root)

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
RULE_TYPES="$ROOT/Release-NMS-Server/common/ruletypes.h"
QUEST="$ROOT/Release-NMS-Quests/global/global_npc.pl"

fails=0
pass() { echo "PASS  $1"; }
fail() { echo "FAIL  $1"; fails=$((fails + 1)); }

for f in "$RULE_TYPES" "$QUEST"; do
        if [ ! -f "$f" ]; then
                echo "FAIL  missing required file: $f"
                exit 1
        fi
done

echo "=== Phase A personal-loot gate invariants ==="

# --- 1. rule declared, default false -----------------------------------
# Match the declaration line only. The description text is intentionally not
# constrained: it is prose and may legitimately change.
if grep -Eq '^RULE_BOOL\(Custom,[[:space:]]*UsePersonalLoot,[[:space:]]*false,' "$RULE_TYPES"; then
        pass "1. Custom:UsePersonalLoot declared RULE_BOOL(Custom,...,false,) in ruletypes.h"
else
        fail "1. Custom:UsePersonalLoot missing from ruletypes.h or its default is not false"
fi

# --- 2. quest reads the rule (not a data bucket) and requires "true" ---
if grep -Fq 'quest::get_rule("Custom:UsePersonalLoot") eq "true"' "$QUEST"; then
        pass '2. global_npc.pl gates on quest::get_rule("Custom:UsePersonalLoot") eq "true"'
else
        fail '2. global_npc.pl does not gate on quest::get_rule("Custom:UsePersonalLoot") eq "true"'
fi

# --- 3. raid bypass precedes the rule gate inside nms_share_corpse_loot --
# Isolate the sub body, drop comment-only lines (the header block documents the
# rule name and would otherwise match before the real statement), then compare
# 1-based line numbers within what remains.
SUB=$(sed -n '/^sub nms_share_corpse_loot/,/^}/p' "$QUEST" \
        | grep -v -E '^[[:space:]]*#')
raid_line=$(printf '%s\n' "$SUB" | grep -n -F '$killer->GetRaid()' | head -1 | cut -d: -f1)
rule_line=$(printf '%s\n' "$SUB" | grep -n -F 'quest::get_rule("Custom:UsePersonalLoot")' | head -1 | cut -d: -f1)

if [ -z "$raid_line" ]; then
        fail "3. no \$killer->GetRaid() raid bypass found inside nms_share_corpse_loot"
elif [ -z "$rule_line" ]; then
        fail "3. no Custom:UsePersonalLoot rule gate found inside nms_share_corpse_loot"
elif [ "$raid_line" -lt "$rule_line" ]; then
        pass "3. raid bypass (line $raid_line) precedes the rule gate (line $rule_line)"
else
        fail "3. raid bypass (line $raid_line) does NOT precede the rule gate (line $rule_line)"
fi

# --- 4. no active legacy activation gate remains -------------------------
# Strip comment-only lines first so the documented-inert buckets in the header
# block do not register as failures. Any surviving *code* read is a real gate.
legacy=$(grep -n -E 'quest::get_data\("(sharedloot|sharedloot_self)"\)' "$QUEST" \
        | grep -v -E '^[[:space:]]*[0-9]+:[[:space:]]*#')
if [ -z "$legacy" ]; then
        pass "4. no active quest::get_data(\"sharedloot\"/\"sharedloot_self\") activation gate remains"
else
        fail "4. legacy activation gate still present:"
        printf '%s\n' "$legacy" | sed 's/^/        /'
fi

echo "=== done ==="
if [ "$fails" -eq 0 ]; then
        echo "RESULT: PASS (4/4 invariants)"
        exit 0
fi
echo "RESULT: FAIL ($fails invariant(s) failed)"
exit 1