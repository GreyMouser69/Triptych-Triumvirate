# Dimensional Pocket Vault -- shared helpers.
#
# PORTED FROM BFE (Brother's Foothold Evolved), Patch 4.
# Reference: D:\Bfoothold_Evolved\quests\plugins\NMS_vault_utils.pl
# Triptych adaptations are marked "Triptych adaptation" at each site. The
# substantive ones:
#   1. VaultCharCanUseItem uses $client->GetClassesBitmask() (multiclass).
#   2. The Merchant button summons the existing Triptych Clockwork Resupply
#      Agent (NPC 771). NPC 151259 / merchant list 382051 are NOT introduced.
#   3. NPCSayTo is added to Release-NMS-Server/zone/perl_client.cpp in this patch.
# Everything else -- slot geometry, storage semantics, refusals, transactional
# writes, the parking-key move, the stash order -- is carried over unchanged.
#
# NOTE on inline citations: comments of the form `file.cpp:1234` are BFE line
# numbers, carried over verbatim from the reference file. They are here to record
# WHY a behaviour exists, not to be looked up in this tree. The two exceptions are
# the explicitly rewritten ones (VaultCharCanUseItem, the merchant block), which
# cite Triptych paths.
#
# The vault UI lives entirely in the client DLL (no source). It is a thin
# view over a server-authoritative store: the server pushes `VAULTDATA|` lines and
# the DLL emits `/say #vault_*` for every click.
#
# Transport constraint: the DLL's chat hook is on real NPC speech. $npc->Say()
# reaches it because Mob::Say -> MessageCloseString -> OP_FormattedMessage, while
# $client->Message() is OP_SpecialMesg and is never seen. So every payload has to
# look like it came out of an NPC's mouth.
#
# It no longer has to be BROADCAST like one, though. Say hands the packet to every
# client within 200 units, which is why, in BFE, every DLL user standing near you
# watched your vault open, page and fill with your items, and everyone without the
# DLL read the raw `VAULTDATA|ADD|...` lines as chat spam.
# $client->NPCSayTo(name, text) (added by this patch to
# Release-NMS-Server/zone/perl_client.cpp) builds the identical
# OP_FormattedMessage and queues it to that one client. VaultSayLines uses it
# whenever the zone binary provides it and falls back to Say otherwise, so quests
# and binary can be rolled out in either order.
#
# No `use strict` here: quest globals like $entity_list are injected by the quest
# system and are reached through plugin::val(), matching every other NMS_*.pl.

# Pick the NPC that will speak for us: the player's target if it is an NPC,
# otherwise the nearest NPC. Range-limited because Say only carries 200 units.
# When nothing is in range VaultSayLines spawns its own speaker at the player --
# an NPC across the zone would speak to nobody.
#
# Returns the NPC, or undef if there is none in range.
sub VaultSpeaker {
    my ($client) = @_;
    return undef unless $client;

    my $speaker = $client->GetTarget();
    return $speaker if ($speaker && $speaker->IsNPC());

    my $el = plugin::val('entity_list');
    return undef unless $el;

    $speaker = undef;
    my $best_d = 200 * 200;
    foreach my $n ($el->GetNPCList()) {
        next unless $n;
        my $dx = $n->GetX() - $client->GetX();
        my $dy = $n->GetY() - $client->GetY();
        my $d  = ($dx * $dx) + ($dy * $dy);
        if ($d < $best_d) { $best_d = $d; $speaker = $n; }
    }
    return $speaker;
}

# The invisible NPC spawned when nobody else is around to talk. Its NPC type is
# part of the Patch 1 schema work (Triptych "Vault Speaker"); this plugin only
# references the id and creates nothing in the database itself.
use constant VAULT_SPEAKER_NPC => 151258;

# Put a speaker AT the player.
#
# The 200-unit limit in VaultSpeaker is how far NPC Say carries. With NPCSayTo
# the distance no longer matters for delivery, but the speaker's name still goes
# on the wire and the Say fallback still needs range, so the search and the
# on-demand speaker are kept as they were.
#
# Never call this directly. VaultSayLines owns the returned NPC's lifetime.
sub VaultSpawnSpeaker {
    my ($client) = @_;
    return undef unless $client;

    my $eid = quest::spawn2(
        VAULT_SPEAKER_NPC, 0, 0,
        $client->GetX(), $client->GetY(), $client->GetZ(), $client->GetHeading()
    );
    return undef unless $eid;

    my $el = plugin::val('entity_list');
    return undef unless $el;

    return $el->GetMobID($eid);
}

# THE ONLY PLACE THE VAULT SPEAKS.
#
# Everything funnels through here so the temporary speaker's spawn and depop sit
# in one function. They were deliberately not spread across the four callers:
# one path that forgets to depop leaves an invisible NPC standing in the world
# forever, and it would be nearly impossible to notice.
sub VaultSayLines {
    my ($client, $lines) = @_;
    return 0 unless ($client && $lines && @{$lines});

    my $speaker = plugin::VaultSpeaker($client);
    my $temp    = 0;

    unless ($speaker) {
        $speaker = plugin::VaultSpawnSpeaker($client);
        $temp    = 1 if $speaker;
    }

    unless ($speaker) {
        $client->Message(13,
            "[Vault] The vault could not be opened here - no speaker could be created.");
        return 0;
    }

    # Deliver to this client only. The speaker is still resolved above so the
    # line carries a real NPC's name exactly as Say would send it: the DLL has
    # no source, so whether it checks the name is unknown and not worth a gamble.
    # An old zone binary without NPCSayTo gets the historical broadcast, and the
    # player is told once per zone process so the mismatch cannot go unnoticed.
    my $direct = $client->can('NPCSayTo') ? 1 : 0;
    if (!$direct && !$VAULT_WARNED_BROADCAST) {
        $VAULT_WARNED_BROADCAST = 1;
        $client->Message(13,
            "[Vault] This zone binary has no NPCSayTo - vault lines are being broadcast to nearby players.");
    }
    my $name = $speaker->GetCleanName();

    # Tight loop -- do not add anything here.
    foreach my $p (@{$lines}) {
        if ($direct) { $client->NPCSayTo($name, $p); }
        else         { $speaker->Say($p); }
    }

    # Say sends its packets synchronously, so the burst is already on the wire.
    # Depop only flags the entity for removal on the next cleanup pass; it does
    # not free it underneath us here.
    $speaker->Depop(0) if $temp;

    return 1;
}

# Speak a whole sequence with NOTHING between the Say() calls.
#
# The tight loop is load-bearing, not stylistic. plugin::LoadMysql re-reads
# eqemu_config.json from disk and opens a fresh DBI connection every call, and
# doing any of that BETWEEN the Say()s is enough to stop the client rendering the
# burst at all. Resolve the speaker once, then emit with nothing in between.
sub VaultSayBurst {
    my ($client, $payloads) = @_;
    return 0 unless ($client && $payloads && @{$payloads});

    return plugin::VaultSayLines($client, $payloads);
}

# Speak one VAULTDATA payload. Returns 1 on success, 0 if no NPC was in range.
#
# Never fails silently: a vault that appears to accept an operation it cannot
# render is indistinguishable from item loss, so the player is always told.
sub VaultSay {
    my ($client, $payload) = @_;
    return 0 unless ($client && defined $payload);

    return plugin::VaultSayLines($client, [ $payload ]);
}

# Icon id for an item, memoised for the life of the zone process.
#
# This matters more than it looks: the DLL ACCEPTS an ADD with icon 0 but draws
# the slot blank, so a zero icon is indistinguishable from a rejected line. Any
# caller must therefore be guaranteed a non-zero value -- hence the fallback.
# Cache lives in a package variable, not a file-scoped `my`. A lexical closed over
# by a named sub misbehaves when the plugin is re-eval'd on #reload quest, and this
# sub sits in the path of every render -- it must not be the thing that breaks.
our %VAULT_ICON_CACHE;

sub VaultIcon {
    my ($item_id) = @_;
    return 639 unless ($item_id && $item_id > 0);
    return $VAULT_ICON_CACHE{$item_id} if exists $VAULT_ICON_CACHE{$item_id};

    # Wrapped: a die() in here would abort the caller's whole EVENT_SAY with no
    # visible error, which is exactly how this failed the first time.
    my $icon = 0;
    eval {
        my $dbh = plugin::LoadMysql();
        if ($dbh) {
            my $sth = $dbh->prepare("SELECT icon FROM items WHERE id = ? LIMIT 1");
            if ($sth && $sth->execute($item_id)) {
                my $row = $sth->fetchrow_hashref();
                $icon = $row->{icon} if ($row && $row->{icon});
                $sth->finish();
            }
            $dbh->disconnect();
        }
        1;
    };

    # 639 is a known-good generic icon. Never return 0: the DLL accepts icon 0 but
    # draws the slot blank, which is indistinguishable from a rejected line.
    $icon = 639 unless ($icon && $icon > 0);
    $VAULT_ICON_CACHE{$item_id} = $icon;
    return $icon;
}

# Convenience for the probes: speak and report which NPC carried it, so a failed
# experiment is never mistaken for a failed protocol.
sub VaultSayVerbose {
    my ($client, $payload) = @_;
    return 0 unless ($client && defined $payload);

    my $ok = plugin::VaultSayLines($client, [ $payload ]);
    $client->Message(15, "[vault] spoke: $payload") if $ok;
    return $ok;
}

# ---------------------------------------------------------------------------
# The vault proper.
#
# Protocol, confirmed live in BFE on 2026-08-31:
#   - ADD is only honoured as a REPLY to the DLL's own `#vault_page <n>`.
#     Unsolicited pushes are silently dropped.
#   - Field 0 of ADD is the page (1-6 Bags, 7-8 Clicky, 9 Proc Locker).
#   - The icon must be a VALID icon, not merely non-zero -- a wrong icon draws a
#     blank square while still being accepted.
#   - The reply must be spoken by an NPC, back-to-back, with no work in between.
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Slot geometry -- SLOT NUMBERS ARE GLOBAL, NOT PER-PAGE.
#
# From the grid loop at 0x1019a37b:
#     first = (page - 1) * 10 + 1
#     last  = (page == 9) ? first + 2 : page * 10
#     for slot = first .. last
#
# So each page owns a fixed 10-slot window of one global numbering:
#     page 1  ->  1..10        page 6  ->  51..60
#     page 2  -> 11..20        page 7  -> 61..70   (Clicky 1)
#     page 3  -> 21..30        page 8  -> 71..80   (Clicky 2)
#     page 4  -> 31..40        page 9  -> 81..83   (Proc Locker: Pri/Sec/Range)
#     page 5  -> 41..50
#
# This is the real reason only page 1 ever rendered: every ADD we sent used slot
# 1-5, which is page 1's window no matter what the page field said. The `slot`
# column stores the GLOBAL number, so the page is always derivable from it.
# ---------------------------------------------------------------------------

use constant VAULT_PROC_LOCKER_PAGE => 9;

sub VaultPageFirstSlot {
    my ($page) = @_;
    return (($page - 1) * 10) + 1;
}

sub VaultPageLastSlot {
    my ($page) = @_;
    my $first = plugin::VaultPageFirstSlot($page);
    return ($page == VAULT_PROC_LOCKER_PAGE) ? ($first + 2) : ($page * 10);
}

# Which page a global slot belongs to, or 0 if it is outside the vault.
sub VaultPageOfSlot {
    my ($slot) = @_;
    return 0 unless (defined $slot && $slot >= 1 && $slot <= 83);
    return VAULT_PROC_LOCKER_PAGE if ($slot >= 81);
    return int(($slot - 1) / 10) + 1;
}

# Item id on the cursor, or 0 if it is empty.
#
# Client::GetItemIDAt returns INVALID_ID -- not 0 -- for an empty slot
# (inventory.cpp:1066), and that value is TRUTHY in Perl. A bare
# `if ($client->GetItemIDAt(33))` is therefore always true, which is exactly how
# withdraw came to report "your cursor is full" on an empty cursor. Every cursor
# check goes through here.
# Where a deposit takes its item from: the cursor (33 = slotCursor on RoF2), unless
# VaultDepositFromInventory has pointed it at an inventory slot with
# `local $plugin::VAULT_SRC_SLOT`. Every read and the final delete of a deposit go
# through this, so both sources run exactly the same checks.
sub VaultSrcSlot {
    return defined $plugin::VAULT_SRC_SLOT ? $plugin::VAULT_SRC_SLOT : 33;
}

sub VaultCursorItem {
    my ($client) = @_;
    my $id = $client->GetItemIDAt(plugin::VaultSrcSlot());
    return 0 unless defined $id;
    return 0 if ($id <= 0 || $id == 0xFFFFFFFF);   # signed -1 or unsigned max
    return $id;
}

# Everything inside the container on the cursor, as rows shaped like
# nms_vault_bag_items: { bag_slot, item_id, charges, attuned, aug => [6] }.
# Empty list for an empty bag or for anything that is not a bag.
#
# Why this exists: a vault square stores an item ID, not an item instance, so a
# bag used to go in as "this bag" and nothing else. VaultDeposit then cleared
# the cursor with DeleteItemInInventory(33), and the server deletes a
# container's contents along with it (SharedDatabase::DeleteInventorySlot
# removes the bag slots too). Observed live: a player parked their bags in the
# vault, took them back out, and every item that had been inside was gone.
# Now the contents are read here first and written to nms_vault_bag_items
# before the cursor is touched.
#
# Two reads per inner slot, and either finding an item counts: the container
# instance's own GetItem(i), and the cursor bag slot itself (351 + i on RoF2)
# through GetItemAt. One quirk in one of them cannot let an item slip past.
sub VaultCursorBagItems {
    my ($client) = @_;
    my @rows;

    my $bag_id = plugin::VaultCursorItem($client);
    return \@rows unless $bag_id;
    my $cap = plugin::VaultBagSlots($bag_id);
    return \@rows unless $cap > 0;

    my $bag = $client->GetItemAt(plugin::VaultSrcSlot());
    foreach my $i (0 .. $cap - 1) {
        my $inner;
        $inner = $bag->GetItem($i) if $bag;
        $inner = $client->GetItemAt(351 + $i) unless ($inner || plugin::VaultSrcSlot() != 33);
        next unless $inner;
        my $id = $inner->GetID();
        next unless (defined $id && $id > 0 && $id != 0xFFFFFFFF);

        my @aug = (0, 0, 0, 0, 0, 0);
        foreach my $k (0 .. 5) {
            my $a = $inner->GetAugmentItemID($k);
            $aug[$k] = (defined $a && $a > 0 && $a != 0xFFFFFFFF) ? $a : 0;
        }
        push @rows, {
            bag_slot => $i,
            item_id  => $id,
            charges  => ($inner->GetCharges() || 1),
            attuned  => ($inner->IsAttuned() ? 1 : 0),
            aug      => \@aug,
        };
    }
    return \@rows;
}

# Rows the vault holds inside the bag at this page and slot, in bag order.
sub VaultBagRows {
    my ($client, $page, $slot) = @_;
    my @rows;
    my $dbh = plugin::LoadMysql();
    return \@rows unless $dbh;
    my $sth = $dbh->prepare(
        "SELECT bag_slot,item_id,charges,augment_one,augment_two,augment_three,"
      . "augment_four,augment_five,augment_six,attuned FROM nms_vault_bag_items "
      . "WHERE character_id=? AND page=? AND slot=? ORDER BY bag_slot");
    if ($sth && $sth->execute($client->CharacterID(), $page, $slot)) {
        while (my $r = $sth->fetchrow_hashref()) { push @rows, $r; }
        $sth->finish();
    }
    $dbh->disconnect();
    return \@rows;
}

# The first item among these ids that the character could not receive because
# of lore, as "<name>", or undef when none conflict. Mirrors
# Client::CheckLoreConflict (inventory.cpp:328): loregroup 0 is not lore, -1 is
# checked by exact id, anything else by group, everywhere but the shared bank
# (0xF7 = every invWhere flag except invWhereSharedBank). The list itself is
# checked too: two lore items that clash with each other would leave the
# second one unsummoned.
#
# This matters because Client::SummonBaggedItems skips a conflicting item with
# nothing more than a chat line, and skips the whole bag if the bag conflicts.
# Its rows are deleted before it is called, so a skip there would be a loss.
sub VaultLoreBlocker {
    my ($client, @ids) = @_;
    return undef unless @ids;

    my %group;
    my $dbh = plugin::LoadMysql();
    if ($dbh) {
        my $sth = $dbh->prepare(
            "SELECT id, loregroup FROM items WHERE id IN (" . join(",", map { "?" } @ids) . ")");
        if ($sth && $sth->execute(@ids)) {
            while (my $r = $sth->fetchrow_hashref()) { $group{ $r->{id} } = $r->{loregroup} + 0; }
            $sth->finish();
        }
        $dbh->disconnect();
    }

    my $inv = $client->GetInventory();
    my (%seen_id, %seen_group);
    foreach my $id (@ids) {
        my $lg = $group{$id} || 0;
        next if $lg == 0;
        my $clash = 0;
        if ($lg == -1) {
            $clash = 1 if ($seen_id{$id}++ || ($inv && $inv->HasItem($id, 0, 0xF7) >= 0));
        }
        else {
            $clash = 1 if ($seen_group{$lg}++ || ($inv && $inv->HasItemByLoreGroup($lg, 0xF7) >= 0));
        }
        return (quest::getitemname($id) || "item $id") if $clash;
    }
    return undef;
}

# Item name, safe to put on the wire.
#
# The name is parsed with %127[^|], so a literal pipe would truncate the field and
# shift every field after it. ':' and '~' delimit bag-content records, so they are
# stripped too -- an item called "Potion: Minor" would otherwise split into two
# bogus content fields.
sub VaultCleanName {
    my ($item_id) = @_;
    my $name = quest::getitemname($item_id) || "Item $item_id";
    $name =~ s/[|:~]//g;
    return substr($name, 0, 127);
}

# Capacity of a container item, or 0 if it is not a container.
our %VAULT_BAGSLOT_CACHE;

sub VaultBagSlots {
    my ($item_id) = @_;
    return 0 unless ($item_id && $item_id > 0);
    return $VAULT_BAGSLOT_CACHE{$item_id} if exists $VAULT_BAGSLOT_CACHE{$item_id};

    my $slots = 0;
    eval {
        my $dbh = plugin::LoadMysql();
        if ($dbh) {
            my $sth = $dbh->prepare("SELECT bagslots FROM items WHERE id = ? LIMIT 1");
            if ($sth && $sth->execute($item_id)) {
                my $row = $sth->fetchrow_hashref();
                $slots = $row->{bagslots} if ($row && $row->{bagslots});
                $sth->finish();
            }
            $dbh->disconnect();
        }
        1;
    };
    $slots = 0 unless ($slots && $slots > 0);
    $VAULT_BAGSLOT_CACHE{$item_id} = $slots;
    return $slots;
}

# Serialise a bag's contents for ADD field 6:
#   id:qty:icon:innerslot:name~id:qty:icon:innerslot:name
# Field order recovered from the parser at 0x1019b5a0 -- the 4th integer is the
# inner slot (it lands at record+0x24, which is what the renderer matches against
# its 0..capacity-1 loop), NOT the third.
sub VaultBagContents {
    my ($client, $page, $slot) = @_;
    my @parts;
    my $dbh = plugin::LoadMysql();
    return '' unless $dbh;

    my $sth = $dbh->prepare(
        "SELECT bag_slot,item_id,charges FROM nms_vault_bag_items "
      . "WHERE character_id=? AND page=? AND slot=? ORDER BY bag_slot");
    if ($sth && $sth->execute($client->CharacterID(), $page, $slot)) {
        while (my $r = $sth->fetchrow_hashref()) {
            push @parts, join(':', $r->{item_id}, ($r->{charges} || 1),
                                   plugin::VaultIcon($r->{item_id}),
                                   $r->{bag_slot},
                                   plugin::VaultCleanName($r->{item_id}));
        }
        $sth->finish();
    }
    $dbh->disconnect();
    return join('~', @parts);
}

# ---------------------------------------------------------------------------
# Clicky buff pages (7 and 8)
#
# The DLL's own tooltip: "Clicky buff bags placed in these pages will
# automatically be loaded upon zoning or relogging." None of this is client-side
# -- the DLL has no clicky-specific command and treats 7/8 as ordinary pages. The
# whole mechanic is ours.
# ---------------------------------------------------------------------------

use constant VAULT_CLICKY_PAGES => (7, 8);

# The click effect of an item: (spell_id, required_level, cap_ticks), or () if it
# has none.
#
# cap_ticks is spells_new.buffduration -- the spell's OWN maximum duration. Buffs
# normally last CalcBuffDuration_formula(level, ...) capped by it (spells.cpp:3308),
# so at low level you get a fraction of the cap: formula 3 is 30*level ticks, which
# is 9 minutes at level 3 regardless of a 27- or 60-minute cap. Passing the cap as
# an explicit duration lands the buff at full length instead.
our %VAULT_CLICK_CACHE;

sub VaultClickEffect {
    my ($item_id) = @_;
    return () unless ($item_id && $item_id > 0);

    unless (exists $VAULT_CLICK_CACHE{$item_id}) {
        my @found = ();
        eval {
            my $dbh = plugin::LoadMysql();
            if ($dbh) {
                my $sth = $dbh->prepare(
                    "SELECT i.clickeffect, i.clicklevel2, s.buffduration "
                  . "FROM items i LEFT JOIN spells_new s ON s.id = i.clickeffect "
                  . "WHERE i.id = ? LIMIT 1");
                if ($sth && $sth->execute($item_id)) {
                    my $r = $sth->fetchrow_hashref();
                    if ($r && $r->{clickeffect} && $r->{clickeffect} > 0) {
                        @found = ($r->{clickeffect}, $r->{clicklevel2} || 0,
                                  $r->{buffduration} || 0);
                    }
                    $sth->finish();
                }
                $dbh->disconnect();
            }
            1;
        };
        $VAULT_CLICK_CACHE{$item_id} = \@found;
    }
    return @{ $VAULT_CLICK_CACHE{$item_id} };
}

# Every item id sitting on a clicky page. Squares only -- bags are refused there,
# so there is nothing nested to look inside.
sub VaultClickyItems {
    my ($client) = @_;
    my @ids;
    my $dbh = plugin::LoadMysql();
    return \@ids unless $dbh;

    my $pages = join(',', VAULT_CLICKY_PAGES);
    my $sth   = $dbh->prepare(
        "SELECT item_id FROM nms_vault WHERE character_id=? AND page IN ($pages)");
    if ($sth && $sth->execute($client->CharacterID())) {
        while (my $r = $sth->fetchrow_hashref()) { push @ids, $r->{item_id}; }
        $sth->finish();
    }
    $dbh->disconnect();
    return \@ids;
}

# Apply the click effect of everything on the clicky pages. Called on zone-in.
#
# ApplySpell rather than CastSpell: this is a convenience re-buff, so it should
# not consume a cast, be interruptible, or be refused while moving.
sub VaultClickyLoad {
    my ($client) = @_;
    return 0 unless $client;

    my $level   = $client->GetLevel();
    my $applied = 0;
    my %seen;

    foreach my $item_id (@{ plugin::VaultClickyItems($client) }) {
        # A bag can legitimately hold two of the same clicky; casting it twice
        # just refreshes the same buff, so collapse duplicates.
        next if $seen{$item_id}++;

        my ($spell_id, $req_level, $cap_ticks) = plugin::VaultClickEffect($item_id);
        next unless $spell_id;

        # Honour the item's own click level -- otherwise the vault becomes a way
        # to wear buffs you could never click yourself.
        next if ($req_level && $level < $req_level);

        # Re-check usability on every load, quietly. Deposit already gates this,
        # but a character's classes can change afterwards (AddClass/RemoveClass),
        # and stored items must not keep granting effects the character has since
        # lost the right to use.
        next unless plugin::VaultCharCanUseItem($client, $item_id, 1);

        # Explicit duration = the spell's own cap, so vault buffs land at full
        # length rather than the level-scaled fraction. ApplySpellBuff treats
        # duration 0 as "normal formula" and anything <= -1 as permanent, so only
        # a positive cap is passed through (spell_effects.cpp:11294).
        $client->ApplySpell($spell_id, ($cap_ticks && $cap_ticks > 0) ? $cap_ticks : 0);
        $applied++;
    }

    if ($applied) {
        $client->Message(15, "[Vault] Loaded $applied clicky buff"
                           . ($applied == 1 ? "" : "s") . " from your vault.");
    }
    return $applied;
}

# Item restriction data: (classes_bitmask, races_bitmask, required_level).
our %VAULT_RESTRICT_CACHE;

sub VaultItemRestrictions {
    my ($item_id) = @_;
    return (0, 0, 0) unless ($item_id && $item_id > 0);

    unless (exists $VAULT_RESTRICT_CACHE{$item_id}) {
        my @r = (0, 0, 0);
        eval {
            my $dbh = plugin::LoadMysql();
            if ($dbh) {
                my $sth = $dbh->prepare(
                    "SELECT classes, races, reqlevel FROM items WHERE id = ? LIMIT 1");
                if ($sth && $sth->execute($item_id)) {
                    my $row = $sth->fetchrow_hashref();
                    @r = ($row->{classes} || 0, $row->{races} || 0, $row->{reqlevel} || 0) if $row;
                    $sth->finish();
                }
                $dbh->disconnect();
            }
            1;
        };
        $VAULT_RESTRICT_CACHE{$item_id} = \@r;
    }
    return @{ $VAULT_RESTRICT_CACHE{$item_id} };
}

# Can THIS character actually use the item? Returns 1, or 0 with a reason.
#
# Triptych adaptation of the class lookup. The class bitmask must come from
# $client->GetClassesBitmask() (Perl_Client_GetClassesBitmask ->
# Client::GetClassesBits, perl_client.cpp:2223) and never from GetClass():
# multiclassing forces the single-class field to the Bard sentinel, so testing it
# would let any character wear anything and bar anything. GetClassBitmask() is the
# same single-class field and is equally wrong here. No GestaltClasses bucket
# fallback is taken -- GetClassesBits() already reads that bitmask server-side.
#
# Encoding is unchanged and matches items.classes exactly -- bit (class_id - 1).
# Eligibility is the multiclass rule: ANY owned class that may use the item.
sub VaultCharCanUseItem {
    my ($client, $item_id, $quiet) = @_;

    my ($classes, $races, $req_level) = plugin::VaultItemRestrictions($item_id);

    if ($req_level && $client->GetLevel() < $req_level) {
        $client->Message(13, "[Vault] You must be level $req_level to use that item.") unless $quiet;
        return 0;
    }

    # 0 means "no restriction recorded" rather than "no class may use it";
    # treating it as a refusal would reject quest and container items.
    #
    # Triptych adaptation: the class bitmask comes from GetClassesBitmask(), which
    # is Client::GetClassesBits() -- the real owned-class bitmask from
    # m_pp.classes. It is NOT the single-class GetClass() field (forced to the
    # Bard sentinel while multiclassing is on, world/client.cpp:1990) and NOT the
    # Perl GetClassBitmask(), which is that same single field. It also needs no
    # GestaltClasses bucket fallback: the server already reads that bucket here.
    # The intersection test against items.classes is unchanged -- an item is
    # usable if ANY owned class may wear it.
    if ($classes) {
        my $mine = $client->GetClassesBitmask();
        $mine = 0 unless (defined $mine && $mine =~ /^\d+$/);
        unless ($mine && ($classes & $mine)) {
            $client->Message(13, "[Vault] Your class cannot use that item.") unless $quiet;
            return 0;
        }
    }

    if ($races) {
        my $race_bit = $client->GetRaceBitmask();
        unless ($race_bit && ($races & $race_bit)) {
            $client->Message(13, "[Vault] Your race cannot use that item.") unless $quiet;
            return 0;
        }
    }

    return 1;
}

# Items the vault refuses outright -- every page, and inside bags too.
#
# Both are LORE, and nothing in the grant path checks lore:
# Client::SummonItemIntoInventory builds the instance and never consults
# loregroup. The vault is also invisible to Client::CountItem, which walks worn
# slots, general slots, bags, bank, bank bags and shared bank and stops there.
# Put those together and a player who parks one of these in the vault reads as
# "does not have it" to any replacement check, is handed a fresh copy, withdraws
# the original, and is holding two of a LORE item.
#
# Refusing storage is the fix rather than teaching a replacement script to read
# the vault, because those scripts are Lua and cannot reach these plugins. It is
# also right on its own terms: the vault key item is what opens the vault, so
# putting it inside is a trap regardless of lore.
#
# Triptych adaptation: the BLOCKED IDS are carried over from BFE unchanged, as
# Patch 4 requires. Only the player-facing text was generalised, because BFE's
# message named Jolum (a BFE Bazaar NPC) who does not exist on Triptych. If
# Triptych uses different item ids for these two, change the keys here and nowhere
# else -- VaultAcceptsAtAll is the only consumer.
our %VAULT_NEVER_STORE = (
    38484 => 'The Freedom Fighter Vault',
    18471 => 'A Faded Writ',
);

sub VaultAcceptsAtAll {
    my ($client, $item_id) = @_;

    my $what = $VAULT_NEVER_STORE{$item_id};
    return 1 unless $what;

    $client->Message(13, "[Vault] $what cannot be stored here. "
                       . "It is lore, so losing it means losing it for good.");
    return 0;
}

# Is this item allowed on a clicky page? Returns 1, or 0 after telling the player
# why not.
#
# Clicky pages hold click-effect items and nothing else -- no containers. Storing
# a bag there would hide the clickies one level down for no benefit, since the
# page's whole purpose is that its contents fire on zone-in.
sub VaultClickyAccepts {
    my ($client, $item_id) = @_;

    if (plugin::VaultBagSlots($item_id) > 0) {
        $client->Message(13, "[Vault] Bags cannot go on the Clicky pages - place the clicky items themselves.");
        return 0;
    }

    my ($spell_id) = plugin::VaultClickEffect($item_id);
    return 1 if $spell_id;

    $client->Message(13, "[Vault] Only items with a click effect belong on the Clicky pages.");
    return 0;
}

sub VaultIsClickyPage {
    my ($page) = @_;
    foreach my $p (VAULT_CLICKY_PAGES) { return 1 if ($page == $p); }
    return 0;
}

# Tell the server to re-read the Proc Locker after page 9 changes.
#
# Without this the C++ side keeps its cached item copies and the proc it already
# granted, so a deposit or withdraw would not take effect until the next zone.
sub VaultLockerChanged {
    my ($client, $page) = @_;
    return unless ($page && $page == VAULT_PROC_LOCKER_PAGE);
    $client->ReloadNMSVaultLocker();
}

# Does this item belong in the Proc Locker slot that was clicked?
#
# The DLL labels the three slots Pri/Sec/Rng (0x1019a55a) but enforces nothing --
# it will let anything be dropped in any of them -- so the rules are ours. Taken
# from its own tooltip: a weapon with a proc in Pri, a shield in Sec, and by
# symmetry a range item in Rng.
sub VaultProcLockerAccepts {
    my ($client, $item_id, $slot) = @_;

    my ($itemtype, $proceffect) = (0, 0);
    eval {
        my $dbh = plugin::LoadMysql();
        if ($dbh) {
            my $sth = $dbh->prepare(
                "SELECT itemtype, proceffect FROM items WHERE id = ? LIMIT 1");
            if ($sth && $sth->execute($item_id)) {
                my $r = $sth->fetchrow_hashref();
                if ($r) {
                    $itemtype   = $r->{itemtype}   || 0;
                    $proceffect = $r->{proceffect} || 0;
                }
                $sth->finish();
            }
            $dbh->disconnect();
        }
        1;
    };

    # Pri and Rng both grant the stored item's PROC and nothing else, so the only
    # thing either needs is a proc -- any weapon the character can use will do.
    # Not restricted to the matching equipment slot: a range item in Rng would be
    # the obvious choice, but nothing in the mechanic requires it, and the C++ side
    # only ever asks whether the item has a combat proc. The usability check
    # (class/race/level) is applied separately for page 9 in VaultDeposit.
    if ($slot == 81 || $slot == 83) {
        unless ($proceffect > 0) {
            my $which = ($slot == 81) ? "Pri" : "Rng";
            $client->Message(13, "[Vault] The $which slot grants the item's proc - that one has none.");
            return 0;
        }
        return 1;
    }

    # Sec is the exception: it grants STATS, and the tooltip is explicit that it is
    # for shields. itemtype 8 = shield.
    if ($slot == 82 && $itemtype != 8) {
        $client->Message(13, "[Vault] The Sec slot takes shields only.");
        return 0;
    }

    return 1;
}

# The clockwork vendor a player can summon from the vault's Merchant button.
#
# Triptych adaptation: BFE had two agents -- 771 (Resupply) and 151259 (Augment,
# a Gemcrafter Anuk stock clone). We deliberately keep ONLY the existing Triptych
# Resupply Agent (NPC 771): NPC 151259 is not part of this patch, no merchant list
# is cloned, and no global/151259.pl is added. 771 already exists here with its own
# owner/depop convention (global/771.pl, global/spells/16995.pl), so this reuses
# that convention exactly rather than inventing a parallel one.
#
# Owned by the summoner (entity variable "owner" = character id), so
# quests/global/771.pl depops it when the owner leaves the zone.
use constant VAULT_RESUPPLY_AGENT_NPC => 771;

# Summon the player's clockwork resupply agent next to them.
#
# Only one per character: every agent this character owns is depopped first,
# otherwise alternating the AA and the vault button would litter the zone with
# vendors that never clean up. Mirrors global/spells/16995.pl.
sub VaultSummonMerchant {
    my ($client) = @_;
    return 0 unless $client;

    my $el = plugin::val('entity_list');
    return 0 unless $el;

    my $char_id = $client->CharacterID();
    foreach my $m ($el->GetMobList()) {
        next unless $m;
        next unless $m->GetNPCTypeID() == VAULT_RESUPPLY_AGENT_NPC;
        my $owner = $m->GetEntityVariable("owner");
        next unless (defined $owner && $owner ne '' && $owner == $char_id);
        $m->Depop();
    }

    # Offset so the agent never lands exactly on the player.
    my $x = $client->GetX() + (rand(10) - 5);
    my $y = $client->GetY() + (rand(10) - 5);
    my $z = $client->GetZ();

    my $id = quest::spawn2(VAULT_RESUPPLY_AGENT_NPC, 0, 0, $x, $y, $z, rand(520));
    return 0 unless $id;

    my $agent = $el->GetMobByID($id);
    if ($agent) {
        $agent->TempName($client->GetCleanName() . "'s Clockwork Resupply Agent");
        $agent->SetEntityVariable("owner", $char_id);
    }
    return 1;
}

# Retained name so the AA path and any external caller keep working. AA 8081
# "Summon Resupply Agent" (spell 16995) does its own spawning in
# quests/global/spells/16995.pl and does not call this, but the name costs
# nothing to keep and preserves BFE's plugin surface.
sub VaultSummonResupplyAgent {
    my ($client) = @_;
    return plugin::VaultSummonMerchant($client);
}

# Which page the player is currently looking at. Needed because #vault_deposit
# and #vault_withdraw carry a slot but no page.
sub VaultCurrentPage {
    my ($client, $set) = @_;
    if (defined $set) {
        $client->SetBucket("vaultpage", $set);
        return $set;
    }
    my $p = $client->GetBucket("vaultpage");
    return ($p && $p >= 1 && $p <= 9) ? $p : 1;
}

# All rows for one page, ordered by slot.
sub VaultRows {
    my ($client, $page) = @_;
    my @rows;
    my $dbh = plugin::LoadMysql();
    return \@rows unless $dbh;

    my $sth = $dbh->prepare(
        "SELECT slot,item_id,charges,augment_one,augment_two,augment_three,"
      . "augment_four,augment_five,augment_six,attuned "
      . "FROM nms_vault WHERE character_id=? AND page=? ORDER BY slot");
    if ($sth && $sth->execute($client->CharacterID(), $page)) {
        while (my $r = $sth->fetchrow_hashref()) { push @rows, $r; }
        $sth->finish();
    }
    $dbh->disconnect();
    return \@rows;
}

# Answer a #vault_page request: CLEAR, OPEN|<page>, then one ADD per stored row.
#
# Everything is assembled BEFORE any Say, because DB work must not be interleaved
# with the burst -- doing so stops the grid rendering entirely.
sub VaultRender {
    my ($client, $page) = @_;
    # Set (local) by #vault_lootstash: storing loot must not pop the vault window open.
    return 1 if $plugin::VAULT_QUIET;
    $page = 1 unless ($page && $page >= 1 && $page <= 9);

    my $rows = plugin::VaultRows($client, $page);

    my @burst = ("VAULTDATA|CLEAR", "VAULTDATA|OPEN|$page");
    foreach my $r (@{$rows}) {
        my $name = plugin::VaultCleanName($r->{item_id});
        my $icon = plugin::VaultIcon($r->{item_id});
        my $qty  = $r->{charges} || 1;

        # Always all 15 fields (the client spec, C:\eqdev\re\spec\01_protocol.md §3.2):
        #   0 page, 1 global slot, 2 item id, 3 qty, 4 icon, 5 name,
        #   6 bag contents (empty for a non-bag),
        #   7-12 AUGMENT ids 1-6 -- the client puts them in the right-click inspect
        #        link (0x1019b880), so sending 0 there showed every vaulted item
        #        without its augments,
        #   13 unread flag (0), 14 bag capacity (0 = not a bag).
        # A square is only drawn as a container when field 14 is > 0 (entry+0x50,
        # checked at 0x1019a8b7). Sending all 15 also keeps the NPC speech's closing
        # quote off the item name: it lands on field 14, which atoi ignores.
        my $cap = plugin::VaultBagSlots($r->{item_id});
        my $contents = $cap > 0 ? plugin::VaultBagContents($client, $page, $r->{slot}) : '';
        push @burst, join('|', "VAULTDATA|ADD", $page, $r->{slot}, $r->{item_id},
                               $qty, $icon, $name, $contents,
                               map({ $r->{$_} || 0 } qw(augment_one augment_two augment_three
                                                        augment_four augment_five augment_six)),
                               0, $cap);
    }

    return plugin::VaultSayLines($client, \@burst);
}

# First free GLOBAL slot within a page's window, or 0 if the page is full.
sub VaultFreeSlot {
    my ($client, $page) = @_;
    my %used = map { $_->{slot} => 1 } @{ plugin::VaultRows($client, $page) };
    foreach my $s (plugin::VaultPageFirstSlot($page) .. plugin::VaultPageLastSlot($page)) {
        return $s unless $used{$s};
    }
    return 0;
}

# Move the cursor item into the current page.
#
# Server-authoritative on purpose: the DLL's own deposit click emits an
# OP_MoveItem into bank range, which Client::SwapItem answers with
# Kick("Inventory desync") when no banker is near (inventory.cpp:1954-1970).
# Doing the move here avoids that path completely.
sub VaultDeposit {
    my ($client, $want_slot) = @_;

    my $item_id = plugin::VaultCursorItem($client);
    unless ($item_id) {
        $client->Message(13, "[Vault] Put an item on your cursor first.");
        return 1;
    }

    return 1 unless plugin::VaultAcceptsAtAll($client, $item_id);

    # $want_slot is the GLOBAL slot the DLL says was clicked, so the page comes
    # from the slot itself rather than from remembered UI state -- one less thing
    # that can be stale. .vaultput names no square, so it falls back to the page
    # being viewed.
    my ($page, $slot);
    if (defined $want_slot && $want_slot =~ /^\d+$/ && plugin::VaultPageOfSlot($want_slot)) {
        $page = plugin::VaultPageOfSlot($want_slot);
        $slot = $want_slot;

        my %used = map { $_->{slot} => 1 } @{ plugin::VaultRows($client, $page) };
        if ($used{$slot}) {
            $client->Message(13, "[Vault] That slot is already occupied.");
            return 1;
        }
    }
    else {
        $page = plugin::VaultCurrentPage($client);
        $slot = plugin::VaultFreeSlot($client, $page);
    }

    # Clicky pages and the Proc Locker grant the item's effect, so the character
    # must genuinely be able to use the item -- otherwise the vault becomes a way
    # to wear effects your class, race or level bars you from.
    if (plugin::VaultIsClickyPage($page) || $page == VAULT_PROC_LOCKER_PAGE) {
        return 1 unless plugin::VaultCharCanUseItem($client, $item_id);
    }

    # Clicky pages take click-effect items only -- no bags, no ordinary gear.
    if (plugin::VaultIsClickyPage($page)) {
        return 1 unless plugin::VaultClickyAccepts($client, $item_id);
    }

    # Proc Locker: each of the three slots takes a specific kind of item.
    if ($page == VAULT_PROC_LOCKER_PAGE) {
        return 1 unless plugin::VaultProcLockerAccepts($client, $item_id, $slot);
    }

    unless ($slot) {
        $client->Message(13, "[Vault] Page $page is full.");
        return 1;
    }

    # A bag with things in it goes in whole: the bag takes the square and each
    # item inside takes a row in nms_vault_bag_items, exactly as if it had been
    # dropped into the bag after the bag was already here. See
    # VaultCursorBagItems for the loss this replaces.
    my $inside = plugin::VaultCursorBagItems($client);
    my $bag_name = quest::getitemname($item_id) || "That bag";
    if (@$inside) {
        # The Proc Locker and the clicky pages judge items one at a time by
        # what they do; a bag full of them has no place there.
        if ($page == VAULT_PROC_LOCKER_PAGE || plugin::VaultIsClickyPage($page)) {
            $client->Message(13, "[Vault] $bag_name has " . scalar(@$inside)
                               . " items inside. Empty it before storing it on this page - nothing was moved.");
            return 1;
        }
        foreach my $r (@$inside) {
            return 1 unless plugin::VaultAcceptsAtAll($client, $r->{item_id});
            if (plugin::VaultBagSlots($r->{item_id}) > 0) {
                $client->Message(13, "[Vault] Containers cannot be placed inside other containers.");
                return 1;
            }
        }
        # Rows left under this square by an older bag would collide with these.
        my $stale = plugin::VaultBagRows($client, $page, $slot);
        if (@$stale) {
            $client->Message(13, "[Vault] Slot $slot still holds " . scalar(@$stale)
                               . " stored items from an earlier bag. Put an empty bag there to reach them first - nothing was moved.");
            return 1;
        }
    }

    my $inst    = $client->GetItemAt(plugin::VaultSrcSlot());
    my $charges = $inst ? ($inst->GetCharges() || 1) : 1;
    my @aug = (0, 0, 0, 0, 0, 0);
    foreach my $i (0 .. 5) {
        # Same INVALID_ID convention as GetItemIDAt -- reject the sentinel in both
        # its signed and unsigned forms, or a bogus augment id gets stored.
        my $a = $client->GetAugmentIDAt(plugin::VaultSrcSlot(), $i);
        $aug[$i] = (defined $a && $a > 0 && $a != 0xFFFFFFFF) ? $a : 0;
    }

    my $dbh = plugin::LoadMysql();
    unless ($dbh) {
        $client->Message(13, "[Vault] Storage unavailable - nothing was moved.");
        return 1;
    }
    # Attunement MUST survive the round trip. The column was here from the start
    # but the insert hardcoded 0, so depositing a soulbound item and taking it
    # back out returned it UNATTUNED -- droppable and tradeable again, which
    # turns the vault into a universal un-soulbinder. VaultWithdraw already
    # restores this value correctly; only the store side was wrong.
    my $attuned = ($inst && $inst->IsAttuned()) ? 1 : 0;

    my $sth = $dbh->prepare(
        "REPLACE INTO nms_vault (character_id,page,slot,item_id,charges,"
      . "augment_one,augment_two,augment_three,augment_four,augment_five,augment_six,attuned) "
      . "VALUES (?,?,?,?,?,?,?,?,?,?,?,?)");
    # The bag row and every content row land together or not at all. A half
    # written bag is exactly the kind of thing that loses items.
    my $ok = 0;
    $dbh->begin_work() if @$inside;
    if ($sth) {
        $ok = $sth->execute($client->CharacterID(), $page, $slot, $item_id, $charges,
                            $aug[0], $aug[1], $aug[2], $aug[3], $aug[4], $aug[5], $attuned);
        $sth->finish();
    }
    if ($ok && @$inside) {
        my $ins = $dbh->prepare(
            "INSERT INTO nms_vault_bag_items (character_id,page,slot,bag_slot,item_id,charges,"
          . "augment_one,augment_two,augment_three,augment_four,augment_five,augment_six,attuned) "
          . "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)");
        $ok = 0 unless $ins;
        foreach my $r (@$inside) {
            last unless $ok;
            my @a = @{ $r->{aug} };
            $ok = $ins->execute($client->CharacterID(), $page, $slot, $r->{bag_slot}, $r->{item_id},
                                $r->{charges}, $a[0], $a[1], $a[2], $a[3], $a[4], $a[5], $r->{attuned});
        }
        $ins->finish() if $ins;
        if ($ok) { $dbh->commit(); } else { $dbh->rollback(); }
    }
    $dbh->disconnect();

    unless ($ok) {
        $client->Message(13, "[Vault] Could not store that item - nothing was moved.");
        return 1;
    }

    # Only remove from the cursor once every row is safely written. For a bag
    # this deletes the contents too, which is fine now: they are all on file.
    $client->DeleteItemInInventory(plugin::VaultSrcSlot(), 0, 1);
    my $with = @$inside ? " with " . scalar(@$inside) . " item" . (@$inside == 1 ? "" : "s") . " inside" : "";
    $client->Message(15, "[Vault] Stored " . (quest::getitemname($item_id) || "item")
                       . "$with in page $page, slot $slot.");
    plugin::VaultLockerChanged($client, $page);
    plugin::VaultRender($client, $page);
    return 1;
}

# Take one slot back out of the vault.
#
# The DLL re-sends #vault_withdraw on every click without waiting for a reply, so
# this must be safe to run repeatedly: a missing row is a no-op, never a second
# item.
sub VaultWithdraw {
    my ($client, $slot) = @_;

    # The slot is global, so it identifies the page on its own.
    my $page = plugin::VaultPageOfSlot($slot);
    return 1 unless $page;

    my ($row) = grep { $_->{slot} == $slot } @{ plugin::VaultRows($client, $page) };
    return 1 unless $row;   # already withdrawn by a repeat click

    if (plugin::VaultCursorItem($client)) {
        $client->Message(13, "[Vault] Your cursor is full - free it before withdrawing.");
        return 1;
    }

    # A bag with vault items inside comes back as one filled bag. Everything
    # that could make the summon drop an item is checked here, before any row
    # goes, because Client::SummonBaggedItems only reports what it skips.
    my $inside = plugin::VaultBagSlots($row->{item_id}) > 0
               ? plugin::VaultBagRows($client, $page, $slot) : [];
    if (@$inside) {
        my $bag_name = quest::getitemname($row->{item_id}) || "That bag";
        # The summon builds a fresh, plain bag: an attuned or augmented bag
        # would come back without that, so it is emptied the usual way instead.
        my $bag_augged = grep { $_ && $_ > 0 } map { $row->{$_} }
            qw(augment_one augment_two augment_three augment_four augment_five augment_six);
        if ($row->{attuned} || $bag_augged) {
            $client->Message(13, "[Vault] $bag_name still has " . scalar(@$inside)
                               . " items in it, and the bag itself is attuned or augmented - take the items out first.");
            return 1;
        }
        my $blocker = plugin::VaultLoreBlocker($client, $row->{item_id}, map { $_->{item_id} } @$inside);
        if (defined $blocker) {
            $client->Message(13, "[Vault] $blocker is lore and you already have one, so the bag cannot come out whole. Take the items out one at a time.");
            return 1;
        }
    }

    # Delete BEFORE summoning, and only hand the item over if the delete actually
    # removed a row. The DLL fires this several times per second, so a check-then-
    # act would duplicate items; making the DELETE the arbiter cannot.
    my $dbh = plugin::LoadMysql();
    unless ($dbh) {
        $client->Message(13, "[Vault] Storage unavailable - nothing was moved.");
        return 1;
    }
    $dbh->begin_work() if @$inside;
    my $sth = $dbh->prepare("DELETE FROM nms_vault WHERE character_id=? AND page=? AND slot=?");
    my $deleted = 0;
    if ($sth) {
        $deleted = $sth->execute($client->CharacterID(), $page, $slot);
        $sth->finish();
    }
    # execute() returns the string "0E0" for zero rows -- true, but numerically 0.
    my $won = ($deleted && $deleted + 0 > 0) ? 1 : 0;
    if (@$inside) {
        my $ok = $won;
        if ($ok) {
            my $del = $dbh->prepare(
                "DELETE FROM nms_vault_bag_items WHERE character_id=? AND page=? AND slot=?");
            $ok = ($del && defined $del->execute($client->CharacterID(), $page, $slot)) ? 1 : 0;
            $del->finish() if $del;
        }
        if ($ok) { $dbh->commit(); } else { $dbh->rollback(); $won = 0; }
    }
    $dbh->disconnect();

    return 1 unless $won;

    if (@$inside) {
        # Client::SummonBaggedItems (client.cpp:13058) builds the bag and its
        # contents with database.CreateItem and pushes the lot onto the cursor.
        # No SummonApocItem, so no upgrade roll -- same property as ReturnItem.
        my @packed = map { {
            item_id => $_->{item_id}, charges => ($_->{charges} || 1),
            attuned => ($_->{attuned} ? 1 : 0),
            augment_one => $_->{augment_one} || 0, augment_two => $_->{augment_two} || 0,
            augment_three => $_->{augment_three} || 0, augment_four => $_->{augment_four} || 0,
            augment_five => $_->{augment_five} || 0, augment_six => $_->{augment_six} || 0,
        } } @$inside;
        $client->SummonBaggedItems($row->{item_id}, \@packed);
        $client->Message(15, "[Vault] Withdrew " . (quest::getitemname($row->{item_id}) || "item")
                           . " with " . scalar(@$inside) . " item" . (@$inside == 1 ? "" : "s") . " inside.");
        plugin::VaultLockerChanged($client, $page);
        plugin::VaultRender($client, $page);
        return 1;
    }

    # MUST be ReturnItem, never SummonItem / SummonItemIntoInventory.
    #
    # Both of those route through Client::SummonApocItem, which applies the loot
    # upgrade roll when Custom:DoItemUpgrades is on (inventory.cpp:269-276:
    # `item_id += 1000000`). That function exists to roll DROPS. Using it to hand
    # back stored property turns the vault into a free item-upgrade machine --
    # deposit, withdraw, and the item comes back a tier higher, repeatable to
    # Legendary. Observed live: a Frostwrath (Enchanted) returned as Legendary.
    #
    # Client::ReturnItem calls SummonItem directly with no roll (inventory.cpp:324).
    # Note the arg order is (id, charges, attuned, aug1..aug6, slot) -- attuned sits
    # third, NOT after the augments. Slot 33 = cursor, matching bank behaviour and
    # the DLL's own "Left Click to withdraw item" tooltip.
    $client->ReturnItem(
        $row->{item_id},
        $row->{charges} || 1,
        $row->{attuned} ? 1 : 0,
        $row->{augment_one}, $row->{augment_two}, $row->{augment_three},
        $row->{augment_four}, $row->{augment_five}, $row->{augment_six},
        33
    );
    $client->Message(15, "[Vault] Withdrew " . (quest::getitemname($row->{item_id}) || "item") . ".");
    plugin::VaultLockerChanged($client, $page);
    plugin::VaultRender($client, $page);
    return 1;
}

# Deposit the cursor item INTO a container already sitting in the vault.
#
# Driven by `#vault_deposit_bagitem_specific <vault_slot> <bag_slot>`, where
# vault_slot is the square the bag occupies on the current page and bag_slot is
# 0-BASED (the DLL loops inner slots 0..capacity-1).
sub VaultBagDeposit {
    my ($client, $vault_slot, $bag_slot) = @_;

    # vault_slot is global, so it names its own page.
    my $page = plugin::VaultPageOfSlot($vault_slot);
    return 1 unless $page;

    my $item_id = plugin::VaultCursorItem($client);
    unless ($item_id) {
        $client->Message(13, "[Vault] Put an item on your cursor first.");
        return 1;
    }

    # The square must really hold a container and the inner slot must be within
    # its capacity. Without this a crafted command could stash an item in a bag
    # that does not exist, where nothing would ever render it again.
    my ($bag) = grep { $_->{slot} == $vault_slot } @{ plugin::VaultRows($client, $page) };
    unless ($bag) {
        $client->Message(13, "[Vault] There is no bag in that slot.");
        return 1;
    }
    my $cap = plugin::VaultBagSlots($bag->{item_id});
    unless ($cap > 0 && $bag_slot >= 0 && $bag_slot < $cap) {
        $client->Message(13, "[Vault] That bag has no such slot.");
        return 1;
    }

    return 1 unless plugin::VaultAcceptsAtAll($client, $item_id);

    # No nesting: the wire format cannot express a bag inside a bag, so one would
    # be stored and then be invisible forever.
    if (plugin::VaultBagSlots($item_id) > 0) {
        $client->Message(13, "[Vault] Containers cannot be placed inside other containers.");
        return 1;
    }

    # Inside a buff bag on a clicky page, every item must actually click.
    if (plugin::VaultIsClickyPage($page)) {
        return 1 unless plugin::VaultClickyAccepts($client, $item_id);
    }

    my $inst    = $client->GetItemAt(plugin::VaultSrcSlot());
    my $charges = $inst ? ($inst->GetCharges() || 1) : 1;
    my @aug = (0, 0, 0, 0, 0, 0);
    foreach my $i (0 .. 5) {
        my $a = $client->GetAugmentIDAt(plugin::VaultSrcSlot(), $i);
        $aug[$i] = (defined $a && $a > 0 && $a != 0xFFFFFFFF) ? $a : 0;
    }

    my $dbh = plugin::LoadMysql();
    unless ($dbh) {
        $client->Message(13, "[Vault] Storage unavailable - nothing was moved.");
        return 1;
    }
    # INSERT, not REPLACE: an occupied inner slot must fail rather than overwrite
    # and destroy whatever was already there.
    # Same attunement fix as VaultDeposit -- a bag item is just as soulbound as
    # one sitting in an ordinary vault slot.
    my $attuned = ($inst && $inst->IsAttuned()) ? 1 : 0;

    my $sth = $dbh->prepare(
        "INSERT INTO nms_vault_bag_items (character_id,page,slot,bag_slot,item_id,charges,"
      . "augment_one,augment_two,augment_three,augment_four,augment_five,augment_six,attuned) "
      . "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)");
    my $ok = 0;
    if ($sth) {
        $ok = eval {
            $sth->execute($client->CharacterID(), $page, $vault_slot, $bag_slot, $item_id,
                          $charges, $aug[0], $aug[1], $aug[2], $aug[3], $aug[4], $aug[5], $attuned);
        };
        $sth->finish();
    }
    $dbh->disconnect();

    unless ($ok) {
        $client->Message(13, "[Vault] That slot is already occupied - nothing was moved.");
        return 1;
    }

    $client->DeleteItemInInventory(plugin::VaultSrcSlot(), 0, 1);
    $client->Message(15, "[Vault] Stored " . (quest::getitemname($item_id) || "item") . " in the bag.");
    plugin::VaultRender($client, $page);
    return 1;
}

# Take an item back out of a container in the vault.
sub VaultBagWithdraw {
    my ($client, $vault_slot, $bag_slot) = @_;

    # vault_slot is global, so it names its own page.
    my $page = plugin::VaultPageOfSlot($vault_slot);
    return 1 unless $page;

    if (plugin::VaultCursorItem($client)) {
        $client->Message(13, "[Vault] Your cursor is full - free it before withdrawing.");
        return 1;
    }

    my $dbh = plugin::LoadMysql();
    unless ($dbh) {
        $client->Message(13, "[Vault] Storage unavailable - nothing was moved.");
        return 1;
    }

    my $row;
    my $sth = $dbh->prepare(
        "SELECT item_id,charges,augment_one,augment_two,augment_three,augment_four,"
      . "augment_five,augment_six,attuned FROM nms_vault_bag_items "
      . "WHERE character_id=? AND page=? AND slot=? AND bag_slot=?");
    if ($sth && $sth->execute($client->CharacterID(), $page, $vault_slot, $bag_slot)) {
        $row = $sth->fetchrow_hashref();
        $sth->finish();
    }
    unless ($row) { $dbh->disconnect(); return 1; }   # repeat click on an empty slot

    # Same rule as the top-level withdraw: let the DELETE arbitrate. The DLL
    # re-sends on every click, so a check-then-act would hand out the item twice.
    my $del = $dbh->prepare(
        "DELETE FROM nms_vault_bag_items WHERE character_id=? AND page=? AND slot=? AND bag_slot=?");
    my $deleted = 0;
    if ($del) {
        $deleted = $del->execute($client->CharacterID(), $page, $vault_slot, $bag_slot);
        $del->finish();
    }
    $dbh->disconnect();
    return 1 unless ($deleted && $deleted + 0 > 0);

    # ReturnItem, never SummonItem -- see VaultWithdraw for why.
    $client->ReturnItem(
        $row->{item_id}, $row->{charges} || 1, $row->{attuned} ? 1 : 0,
        $row->{augment_one}, $row->{augment_two}, $row->{augment_three},
        $row->{augment_four}, $row->{augment_five}, $row->{augment_six}, 33
    );
    $client->Message(15, "[Vault] Withdrew " . (quest::getitemname($row->{item_id}) || "item") . ".");
    plugin::VaultLockerChanged($client, $page);
    plugin::VaultRender($client, $page);
    return 1;
}

1;

# ---------------------------------------------------------------------------
# .vaultfind <text> / #vault_find <text> -- search the vault by item name.
#
# The client's Find Item window indexes only what the client holds; the vault
# is nms_vault + nms_vault_bag_items on the server and reaches the client as
# one page of VAULTDATA lines at a time, so nothing client-side can search it.
# This answers with plain messages (readable in chat, parseable by MacroQuest
# events), one per hit, top-level items and bag contents alike:
#
#   [VaultFind] item page 3 slot 44 id 1234 x1: Sword of Something
#   [VaultFind] bagitem page 3 slot 44 bag 2 id 5678 x20: Bone Chips
#   [VaultFind] 2 matches for 'some'
#
# Slot numbers are the GLOBAL slots the DLL uses, so a hit can be pulled with
# `#vault_withdraw <slot>` or `#vault_withdraw_bagitem <slot> <bag>` without
# the window being open (both are server-authoritative and only need an empty
# cursor). Case-insensitive substring; capped so a one-letter search cannot
# flood the chat window.
# ---------------------------------------------------------------------------
use constant VAULT_FIND_MAX => 40;

sub VaultFind {
    my ($client, $text) = @_;
    $text = '' unless defined $text;
    $text =~ s/^\s+|\s+$//g;
    if (length($text) < 2) {
        $client->Message(13, "[VaultFind] usage: .vaultfind <part of an item name> (at least two letters)");
        return 1;
    }

    my $dbh = plugin::LoadMysql();
    unless ($dbh) {
        $client->Message(13, "[VaultFind] Storage unavailable.");
        return 1;
    }

    my $cid  = $client->CharacterID();
    my $like = '%' . $text . '%';
    my @hits;

    my $sth = $dbh->prepare(
        "SELECT v.page, v.slot, v.item_id, v.charges, i.Name "
      . "FROM nms_vault v JOIN items i ON i.id = v.item_id "
      . "WHERE v.character_id = ? AND i.Name LIKE ? ORDER BY v.page, v.slot");
    if ($sth && $sth->execute($cid, $like)) {
        while (my $r = $sth->fetchrow_hashref()) {
            push @hits, sprintf("[VaultFind] item page %d slot %d id %d x%d: %s",
                $r->{page}, $r->{slot}, $r->{item_id}, ($r->{charges} || 1), plugin::VaultCleanName($r->{item_id}));
        }
        $sth->finish();
    }

    $sth = $dbh->prepare(
        "SELECT b.page, b.slot, b.bag_slot, b.item_id, b.charges, i.Name "
      . "FROM nms_vault_bag_items b JOIN items i ON i.id = b.item_id "
      . "WHERE b.character_id = ? AND i.Name LIKE ? ORDER BY b.page, b.slot, b.bag_slot");
    if ($sth && $sth->execute($cid, $like)) {
        while (my $r = $sth->fetchrow_hashref()) {
            push @hits, sprintf("[VaultFind] bagitem page %d slot %d bag %d id %d x%d: %s",
                $r->{page}, $r->{slot}, $r->{bag_slot}, $r->{item_id}, ($r->{charges} || 1), plugin::VaultCleanName($r->{item_id}));
        }
        $sth->finish();
    }
    $dbh->disconnect();

    my $total = scalar(@hits);
    my $shown = $total > VAULT_FIND_MAX ? VAULT_FIND_MAX : $total;
    $client->Message(15, $hits[$_]) for (0 .. $shown - 1);
    $client->Message(15, sprintf("[VaultFind] %d match%s for '%s'%s", $total, ($total == 1 ? '' : 'es'), $text,
        ($total > $shown ? " (first $shown shown, narrow the search)" : '')));
    return 1;
}

# ---------------------------------------------------------------------------
# .vaultindex / #vault_index -- dump the whole vault, with stats, for an
# indexer (MacroQuest's finditem.lua). Companion to .vaultfind, which is the
# human-facing search.
#
# One item per record, records packed several per line so a full vault is a
# couple of dozen chat lines rather than hundreds:
#
#   [VaultIndex] BEGIN <count>
#   [VaultIndex] R|page|slot|bag|item_id|qty|icon|itemtype|slots|classes|
#                nodrop|magic|lore|tradeskills|stacksize|bagslots|weight|
#                ac|hp|mana|endur|str|sta|agi|dex|wis|int|cha|
#                hstr|hsta|hagi|hdex|hwis|hint|hcha|
#                attack|spelldmg|healamt|mr|fr|cr|dr|pr|damage|delay|name
#                ~R|...
#   [VaultIndex] END
#
# bag is -1 for a top-level slot, else the 0-based bag slot. name is last
# because it is the only free-text field and VaultCleanName strips | ~ :
# from it. Stats are the row of the item actually stored (a Legendary in the
# vault reports its Legendary stats). Withdrawal is `#vault_withdraw <slot>`
# or `#vault_withdraw_bagitem <slot> <bag>` with the values given here.
# ---------------------------------------------------------------------------
use constant VAULT_INDEX_PER_LINE => 6;

sub VaultIndex {
    my ($client) = @_;
    my $dbh = plugin::LoadMysql();
    unless ($dbh) {
        $client->Message(13, "[VaultIndex] Storage unavailable.");
        return 1;
    }
    my $cid  = $client->CharacterID();
    my $cols = "i.icon, i.itemtype, i.slots, i.classes, i.nodrop, i.magic, i.loregroup, i.tradeskills, "
             . "i.stacksize, i.bagslots, i.weight, i.ac, i.hp, i.mana, i.endur, i.astr, i.asta, i.aagi, i.adex, "
             . "i.awis, i.aint, i.acha, i.heroic_str, i.heroic_sta, i.heroic_agi, i.heroic_dex, i.heroic_wis, "
             . "i.heroic_int, i.heroic_cha, i.attack, i.spelldmg, i.healamt, i.mr, i.fr, i.cr, i.dr, i.pr, "
             . "i.damage, i.delay";
    my @recs;
    my $pack = sub {
        my ($page, $slot, $bag, $r) = @_;
        return join('|', 'R', $page, $slot, $bag, $r->{item_id}, ($r->{charges} || 1),
            (map { defined $r->{$_} ? $r->{$_} + 0 : 0 } qw(icon itemtype slots classes nodrop magic loregroup tradeskills
                stacksize bagslots weight ac hp mana endur astr asta aagi adex awis aint acha heroic_str heroic_sta
                heroic_agi heroic_dex heroic_wis heroic_int heroic_cha attack spelldmg healamt mr fr cr dr pr damage delay)),
            plugin::VaultCleanName($r->{item_id}));
    };

    my $sth = $dbh->prepare("SELECT v.page, v.slot, v.item_id, v.charges, $cols FROM nms_vault v JOIN items i ON i.id = v.item_id "
                          . "WHERE v.character_id = ? ORDER BY v.page, v.slot");
    if ($sth && $sth->execute($cid)) {
        while (my $r = $sth->fetchrow_hashref()) { push @recs, $pack->($r->{page}, $r->{slot}, -1, $r); }
        $sth->finish();
    }
    $sth = $dbh->prepare("SELECT b.page, b.slot, b.bag_slot, b.item_id, b.charges, $cols FROM nms_vault_bag_items b JOIN items i ON i.id = b.item_id "
                       . "WHERE b.character_id = ? ORDER BY b.page, b.slot, b.bag_slot");
    if ($sth && $sth->execute($cid)) {
        while (my $r = $sth->fetchrow_hashref()) { push @recs, $pack->($r->{page}, $r->{slot}, $r->{bag_slot}, $r); }
        $sth->finish();
    }
    $dbh->disconnect();

    $client->Message(15, "[VaultIndex] BEGIN " . scalar(@recs));
    while (@recs) {
        my @chunk = splice(@recs, 0, VAULT_INDEX_PER_LINE);
        $client->Message(15, "[VaultIndex] " . join('~', @chunk));
    }
    $client->Message(15, "[VaultIndex] END");
    return 1;
}

# ---------------------------------------------------------------------------
# #vault_move <from> <to> -- move the item in global slot <from> to <to>, or swap
# the two when both are occupied. A bag takes its contents with it. Used by the
# DLL's "Sort page" (and anything else that wants to rearrange the vault).
#
# Pages 1-6 only: the clicky pages (7, 8) and the Proc Locker (9) have deposit
# rules of their own, and a move is not a deposit, so nothing may be moved in
# or out of them this way. One transaction: the source row (and its bag rows)
# parks at page 0 / slot 0, which no real row uses (slots start at 1), the
# destination takes its place, then the parked rows land on the destination.
# Answers by re-rendering the page of <from>, so the DLL's grid follows.
# ---------------------------------------------------------------------------
sub VaultMove {
    my ($client, $from, $to) = @_;
    my $pf = plugin::VaultPageOfSlot($from);
    my $pt = plugin::VaultPageOfSlot($to);
    unless ($pf && $pt && $pf <= 6 && $pt <= 6) {
        $client->Message(13, "[Vault] Items can only be moved between pages 1-6.");
        return 1;
    }
    return 1 if $from == $to;

    my $dbh = plugin::LoadMysql();
    unless ($dbh) {
        $client->Message(13, "[Vault] Storage unavailable - nothing was moved.");
        return 1;
    }
    my $cid = $client->CharacterID();
    my $ok = 1;
    $dbh->begin_work();
    my $moved = $dbh->do("UPDATE nms_vault SET page = 0, slot = 0 WHERE character_id = ? AND page = ? AND slot = ?",
                         undef, $cid, $pf, $from);
    if (!defined $moved || $moved != 1) {
        $ok = 0;   # nothing there (a repeat click), or a parked row was left behind
    }
    else {
        my @steps = (
            ["UPDATE nms_vault_bag_items SET page = 0, slot = 0 WHERE character_id = ? AND page = ? AND slot = ?", $cid, $pf, $from],
            ["UPDATE nms_vault SET page = ?, slot = ? WHERE character_id = ? AND page = ? AND slot = ?", $pf, $from, $cid, $pt, $to],
            ["UPDATE nms_vault_bag_items SET page = ?, slot = ? WHERE character_id = ? AND page = ? AND slot = ?", $pf, $from, $cid, $pt, $to],
            ["UPDATE nms_vault SET page = ?, slot = ? WHERE character_id = ? AND page = 0 AND slot = 0", $pt, $to, $cid],
            ["UPDATE nms_vault_bag_items SET page = ?, slot = ? WHERE character_id = ? AND page = 0 AND slot = 0", $pt, $to, $cid],
        );
        foreach my $st (@steps) {
            my ($sql, @args) = @$st;
            unless (defined $dbh->do($sql, undef, @args)) { $ok = 0; last; }
        }
    }
    if ($ok) { $dbh->commit(); } else { $dbh->rollback(); }
    $dbh->disconnect();

    plugin::VaultRender($client, $pf);
    return 1;
}

# ---------------------------------------------------------------------------
# .vaultstash / #vault_stash -- put the cursor item away wherever there is room.
#
# The DLL's deposit needs a slot: `#vault_deposit` alone takes the first free
# TOP-LEVEL square on the page being viewed, and a page of bags has none, so
# "Page 1 is full" while the bags sit empty. That is the case a batch mover
# hits on every item. This picks the destination itself, in this order:
#
#   1. a free slot inside any bag on pages 1-6 (containers never go in bags)
#   2. a free top-level square on pages 1-6, lowest page first
#
# then hands off to VaultBagDeposit / VaultDeposit, which do every check and
# the actual move exactly as a click would. Pages 7-9 (clicky, proc locker)
# have rules of their own and are never chosen here. Answers with one line an
# indexer can read: "[VaultStash] page P slot S[ bag B]: name" or
# "[VaultStash] FULL". Stacks are not merged into existing stacks.
# ---------------------------------------------------------------------------
# Store one item straight from the character's inventory, the way .vaultstash
# stores the cursor item (bags in the vault first, then a free square, pages 1-6).
# Used by the rebuilt client's "Deposit all" and shift-click:
#   #vault_deposit_inv <general 1-10>          the item in that inventory slot
#   #vault_deposit_inv <general 1-10> <pos>    position <pos> (1-based) in the bag there
# The client names the slot this way, not by server slot id, because bag slot ids
# depend on the server build. Bags themselves are refused: a bag with things in it
# belongs on the cursor path, which keeps its contents.
sub VaultDepositFromInventory {
    my ($client, $general, $pos) = @_;
    unless (defined $general && $general =~ /^\d+$/ && $general >= 1 && $general <= 10) {
        $client->Message(13, "[Vault] That is not an inventory slot.");
        return 1;
    }
    my $slot;
    if (defined $pos && $pos ne '') {
        my $first = quest::getinventoryslotid("general${general}bag.begin");
        my $last  = quest::getinventoryslotid("general${general}bag.end");
        $slot = $first + $pos - 1;
        unless ($pos =~ /^\d+$/ && $pos >= 1 && $slot <= $last) {
            $client->Message(13, "[Vault] That is not a bag slot.");
            return 1;
        }
    }
    else {
        $slot = quest::getinventoryslotid("general${general}");
    }

    my $id = $client->GetItemIDAt($slot);
    unless (defined $id && $id > 0 && $id != 0xFFFFFFFF) {
        $client->Message(13, "[Vault] Nothing there to store.");
        return 1;
    }
    if (plugin::VaultBagSlots($id) > 0) {
        $client->Message(13, "[Vault] " . (quest::getitemname($id) || "That bag")
                           . " is a bag - pick it up and deposit it from the cursor to keep what is inside.");
        return 1;
    }

    local $plugin::VAULT_SRC_SLOT = $slot;
    local $plugin::VAULT_QUIET    = 1;   # one redraw at the end, asked for by the client
    return plugin::VaultStash($client);
}

sub VaultStash {
    my ($client) = @_;
    my $item_id = plugin::VaultCursorItem($client);
    unless ($item_id) {
        $client->Message(13, "[VaultStash] Put an item on your cursor first.");
        return 1;
    }
    return 1 unless plugin::VaultAcceptsAtAll($client, $item_id);
    my $name = plugin::VaultCleanName($item_id);
    my $is_container = plugin::VaultBagSlots($item_id) > 0;

    my $dbh = plugin::LoadMysql();
    unless ($dbh) {
        $client->Message(13, "[VaultStash] Storage unavailable - nothing was moved.");
        return 1;
    }
    my %bag_used;   # "page|slot" -> { bag_slot => 1 }
    my $sth = $dbh->prepare("SELECT page, slot, bag_slot FROM nms_vault_bag_items WHERE character_id = ?");
    if ($sth && $sth->execute($client->CharacterID())) {
        while (my $r = $sth->fetchrow_hashref()) { $bag_used{"$r->{page}|$r->{slot}"}{$r->{bag_slot}} = 1; }
        $sth->finish();
    }
    $dbh->disconnect();

    # 1. inside a bag
    unless ($is_container) {
        foreach my $page (1 .. 6) {
            foreach my $row (@{ plugin::VaultRows($client, $page) }) {
                my $cap = plugin::VaultBagSlots($row->{item_id});
                next unless $cap > 0;
                foreach my $b (0 .. $cap - 1) {
                    next if $bag_used{"$page|$row->{slot}"}{$b};
                    $client->Message(15, "[VaultStash] page $page slot $row->{slot} bag $b: $name");
                    return plugin::VaultBagDeposit($client, $row->{slot}, $b);
                }
            }
        }
    }
    # 2. a top-level square
    foreach my $page (1 .. 6) {
        my $slot = plugin::VaultFreeSlot($client, $page);
        if ($slot) {
            $client->Message(15, "[VaultStash] page $page slot $slot: $name");
            return plugin::VaultDeposit($client, $slot);
        }
    }
    $client->Message(13, "[VaultStash] FULL - no room anywhere on pages 1-6 for $name.");
    return 1;
}
