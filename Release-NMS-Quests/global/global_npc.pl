use List::Util 'min';
sub EVENT_SAY {
    if (plugin::CustomEventSayEntry()) {
        return;
    }
    
    if (plugin::MultiClassingEnabled() && $npc->GetClass() >= 20 && $npc->GetClass() <= 35) {
        my $classes = $client->GetClassesBitmask();
        my $player_class_id = $npc->GetClass() - 19;
        my $class_name = quest::getclassname($player_class_id);

        if ($text=~/hail/i) {
            my $select_string = quest::saylink("class_select", 1, "become a $class_name");
            my %class_greetings = (
                1 => "Ah, a courageous soul approaches. Are you here to embrace the discipline and strength required to [$select_string]?",
                2 => "Blessings upon you, child. The light guides you to me; is it your wish to [$select_string] and serve the divine?",
                3 => "Honor and valor shine from your eyes. Are you destined to [$select_string], a righteous defender of the light?",
                4 => "The winds whisper of a new guardian. Is your heart called to the wilds, to [$select_string], protector of nature?",
                5 => "A shadow looms near. Is it your fate to command the darkness and [$select_string]?",
                6 => "The essence of nature surrounds you. Are you ready to [$select_string], guardian of the balance?",
                7 => "Discipline and inner strength are your allies. Do you seek the path to [$select_string], master of martial arts?",
                8 => "A melody accompanies your steps. Do you feel the rhythm calling you to [$select_string], the voice of inspiration?",
                9 => "Cunning and silence are your markers. Are you prepared to [$select_string], master of stealth and treachery?",
                10 => "The spirits whisper of a new journey. Is it time for you to [$select_string], a conduit of the spirit world?",
                11 => "A chill of the grave precedes you. Will you embrace the dark arts and [$select_string]?",
                12 => "Arcane energies pulse around you. Is your destiny to [$select_string], master of the elements?",
                13 => "Creation's essence swirls around you. Are you called to [$select_string], summoner of the arcane?",
                14 => "Your presence bends reality. Are you ready to [$select_string], weaver of illusions and mind control?",
                15 => "The call of the wild strengthens. Will you heed the call and [$select_string], melding the power of beasts and combat?",
                16 => "Rage burns within your spirit. Do you wish to unleash this power and [$select_string], a warrior of frenzy?"
            );
            
            my $greeting = $class_greetings{$player_class_id} // "Greetings, traveler. Are you seeking guidance or knowledge?";
            if (!($classes & plugin::GetClassBitmask($player_class_id)) && plugin::GetClassesCount($client) < 3) {
                plugin::NPCTell($greeting);
            }
        }        

        if ($text eq "class_select") {
            my $class_name = quest::getclassname($npc->GetClass() - 19);
            my $confirm_link = quest::saylink("class_confirm", 1, "Are you ready to commit to the path of the $class_name?");

            my %class_specific_messages = (
                1 => "The path of the Warrior is arduous and demanding, requiring unwavering courage and strength. " . 
                    "Once chosen, this path is your destiny, only to be reversed at the whims of the gods. [" . $confirm_link . "]",
                2 => "Embracing the Cleric's way means dedicating your life to the divine, serving as a beacon of light and healing. " .
                    "This sacred commitment is binding, revered by the gods themselves. [" . $confirm_link . "]",
                3 => "The Paladin stands as a beacon of hope, blending the might of arms with the purity of faith. " .
                    "Are you prepared to defend the light and uphold justice, knowing such a choice is guided by the gods? [" . $confirm_link . "]",
                4 => "Rangers protect the balance of nature, a path filled with peril and beauty. " .
                    "If your heart is true to the wild, confirm your dedication to become one with nature and its guardians. [" . $confirm_link . "]",
                5 => "Shadow Knights wield the power of darkness and fear. " .
                    "This path is fraught with danger and moral ambiguity. Only the most resolute may walk it, and once chosen, it is rarely abandoned. [" . $confirm_link . "]",
                6 => "Druids are the guardians of nature, harmonizing the forces of life and growth. " .
                    "To walk this path is to become one with the earth itself. Are you ready to embrace this eternal bond? [" . $confirm_link . "]",
                7 => "The Monk's discipline is forged from inner strength and relentless training. " .
                    "Embrace this path with the understanding that it demands complete devotion, a devotion that is recognized by the gods. [" . $confirm_link . "]",
                8 => "Bards are the heart of any fellowship, weaving magic and music into powerful symphonies. " .
                    "If you feel the song within your soul, affirm your desire to live a life of melody and adventure. [" . $confirm_link . "]",
                9 => "Rogues thrive in the shadows, where cunning and agility are the keys to survival. " .
                    "Is your spirit attuned to the silent whispers of the dark? Confirm your path and step into the world unseen. [" . $confirm_link . "]",
                10 => "Shamans act as intermediaries between the physical and spirit worlds. " .
                    "If you are called to bridge these realms, affirm your commitment to the spiritual journey ahead. [" . $confirm_link . "]",
                11 => "Necromancers command the forces of death and decay. " .
                    "This dark path is not chosen lightly, for its course is irrevocable, shadowed by the oversight of the gods themselves. [" . $confirm_link . "]",
                12 => "Wizards master the arcane, wielding the raw forces of magic. " .
                    "If you seek to harness these elemental powers, confirm your resolve to tread a path fraught with danger and discovery. [" . $confirm_link . "]",
                13 => "Magicians shape reality, summoning creatures and objects from the ether. " .
                    "Are you prepared to command the very fabric of existence, knowing such power is watched closely by the divine? [" . $confirm_link . "]",
                14 => "Enchanters twist the minds and reality itself, with wisdom and subtlety. " .
                    "If you choose to weave the strands of fate, know that this path is as binding as the spells you will cast. [" . $confirm_link . "]",
                15 => "Beastlords bond with the spirits of animals, embodying their primal essence. " .
                    "This sacred pact with nature is eternal, guided by the spirits and overseen by the gods. Are you ready to accept this union? [" . $confirm_link . "]",
                16 => "Berserkers unleash their inner fury, a force of pure, unbridled power. " .
                    "This path of rage is relentless and all-consuming. Confirm if you are ready to embrace the storm within, under the gaze of the gods. [" . $confirm_link . "]"
            );

            my $class_message = $class_specific_messages{$player_class_id} // "The path before you is significant, a choice that once made, is not easily undone. The gods watch over your decision. " . $confirm_link;
            plugin::NPCTell($class_message);
        }

        if ($text eq "class_confirm") {
            if (plugin::GetClassesCount($client) < 3) {
                plugin::AddClass($player_class_id);
                plugin::NPCTell("Welcome, $class_name, and be known!");
            }
        }
    }
}

# =========================================================================
# NMS-LOCAL: SHARED LOOT
#
# When a mob dies, EVERY eligible player -- the killer included -- gets their own
# copy of each corpse item delivered to their /nmsloot Pending window (opcode
# 0x140A), and the corpse is emptied.
#
# The killer is deliberately NOT skipped. An earlier version left the corpse
# intact in group mode so the killer could loot it normally, but EQEmu lets any
# group member loot a corpse -- so everyone else could take the original AND
# accept their offer copy. Emptying the corpse and giving everyone an offer is
# the only outcome that cannot duplicate or lose an item.
#
# FAIL-CLOSED MASTER GATE. This is a deliberate DIVERGENCE from BFE, which treats
# an ABSENT 'sharedloot' bucket as ENABLED (it only returns early on "0"). Here
# the bucket must be exactly "1" or nothing runs:
#
#   absent            -> disabled   (DataBucket returns "" for a missing key, so
#                                   absent and empty are indistinguishable)
#   empty string      -> disabled
#   "0"               -> disabled
#   malformed / other -> disabled
#
# The feature therefore ships dark. Server and client patches can be deployed and
# exercised through the Perl bindings without silently changing world loot
# economics on a live shard.
#
# Data buckets read (quest::get_data):
#   sharedloot            1 only. Master switch. Anything else disables.
#   sharedloot_self       1/0  also route loot when SOLO/ungrouped   (default off)
#   sharedloot_maxitems   cap items shared per corpse, 0 = no cap    (default 0)
# =========================================================================
our $nms_offer_seq = 0;

sub nms_share_corpse_loot {
    my ($corpse, $killer_id) = @_;
    return unless ($corpse);

    # ---- FAIL-CLOSED MASTER GATE --------------------------------------
    # Exact match required. See the header note: an absent bucket reads back
    # as an empty string, so "defined and not 0" (BFE's test) would enable
    # the feature on a shard that never configured it.
    my $enabled = quest::get_data("sharedloot");
    return unless (defined($enabled) && $enabled eq "1");

    # $killer_id is whoever landed the KILLING BLOW, which for a pet class is the
    # PET, not its owner. GetClientByID returns nothing for a pet, so this used to
    # return here: the corpse was never emptied and no offers were queued, and the
    # kill fell back to the normal loot window. A magician killing with his pet got
    # the offer window on his own killing blows and the old loot window on his
    # pet's, which is what made it look random. Same for swarm pets, mercenaries
    # and charmed mobs -- anything whose entity id is not a Client.
    my $killer = $entity_list->GetClientByID($killer_id);
    unless ($killer) {
        my $ent = $entity_list->GetMobByID($killer_id);
        my $own = ($ent && $ent->IsPet()) ? $ent->GetOwner() : undef;
        $killer = $own->CastToClient() if ($own && $own->IsClient());
    }
    return unless ($killer);

    my $self_mode = quest::get_data("sharedloot_self");
    $self_mode = (defined($self_mode) && $self_mode eq "1") ? 1 : 0;

    # Everyone eligible, killer included.
    my @members;
    my $group = $killer->IsGrouped() ? $killer->GetGroup() : undef;
    my $raid  = $killer->GetRaid();
    if ($group) {
        push @members, $group->GetMember($_) for (0 .. $group->GroupCount() - 1);
    }
    elsif ($raid) {
        push @members, $raid->GetMember($_) for (0 .. $raid->RaidCount() - 1);
    }
    else {
        return unless ($self_mode);
        push @members, $killer;
    }
    return unless (scalar(@members));

    # ---- Box Looting Control roster --------------------------------------
    # Opcode 0x140D populates the Looters panel with the characters eligible to
    # be "active looter". Sent before the offers so the window has the roster in
    # place by the time rows appear. SendNMSLooterList applies Patch 5's 20-name
    # cap server-side, so no extra trimming is needed here.
    {
        my @names;
        foreach my $m (@members) {
            next unless ($m && $m->IsClient());
            push @names, $m->CastToClient()->GetName();
        }
        if (scalar(@names)) {
            foreach my $m (@members) {
                next unless ($m && $m->IsClient());
                $m->CastToClient()->SendNMSLooterList(join(",", @names));
            }
        }
    }

    # ---- coin -----------------------------------------------------------
    # Separate from the loot list, so it needs its own pass. Split through the
    # group when there is one so EQ's own division still applies.
    #
    # NOTE (unresolved policy, carried over from BFE on purpose): the raid branch
    # does NOT call Raid::SplitMoney, so in a raid every coin goes to the killer
    # alone while everyone else receives an item copy. See the plan's open item
    # "Raid coin policy". Not invented here -- sharedloot ships disabled, and
    # this must be decided and documented before the gate is turned on.
    my $cp = $corpse->GetCopper()   || 0;
    my $sp = $corpse->GetSilver()   || 0;
    my $gp = $corpse->GetGold()     || 0;
    my $pp = $corpse->GetPlatinum() || 0;
    if ($cp || $sp || $gp || $pp) {
        if ($group) {
            $group->SplitMoney($cp, $sp, $gp, $pp, $killer);
        } else {
            $killer->AddMoneyToPP($cp, $sp, $gp, $pp, 1);
            my @parts;
            push @parts, "${pp}p" if ($pp);
            push @parts, "${gp}g" if ($gp);
            push @parts, "${sp}s" if ($sp);
            push @parts, "${cp}c" if ($cp);
            $killer->Message(15, "[Loot] Collected " . join(" ", @parts) . ".");
        }
        $corpse->RemoveCash();
    }

    # ---- items ----------------------------------------------------------
    # GetLootEntries() rather than GetLootList(): the latter DE-DUPLICATES item ids
    # and discards stack sizes, so a corpse holding two of the same drop offered one
    # and left one behind, and a stack of 20 arrows offered a single arrow and left
    # 19 sitting on the corpse. Each entry here is "item_id:charges", one per real
    # loot row, and duplicate rows are processed in turn -- never collapsed.
    my @loot = $corpse->GetLootEntries();
    return unless (scalar(@loot));

    my $cap = quest::get_data("sharedloot_maxitems");
    $cap = 0 unless (defined($cap) && $cap =~ /^\d+$/);

    my $zid = $killer->GetZoneID();
    my $n = 0;
    my %queued;

    foreach my $entry (@loot) {
        my ($item_id, $charges) = split(/:/, $entry);
        next unless ($item_id && $item_id =~ /^\d+$/ && $item_id > 0);
        $charges = 1 unless (defined($charges) && $charges =~ /^\d+$/ && $charges > 0);
        last if ($cap > 0 && $n >= $cap);
        my $iname = quest::getitemname($item_id);
        next unless ($iname);

        my $sent = 0;
        foreach my $m (@members) {
            next unless ($m && $m->IsClient());
            my $mc = $m->CastToClient();
            next unless ($mc->GetZoneID() == $zid);
            next if ($mc->GetHP() <= 0);

            # NOTE: this id is now only a FALLBACK. The server derives the real
            # offer id from the item id (nms_lootoffer.cpp), because the client
            # writes its rules file as Name=Action|icon|<offer id> and matches
            # saved rules by that id -- a per-offer id meant no rule could ever
            # match a later drop, which is why auto-sell/auto-keep never fired.
            $nms_offer_seq = ($nms_offer_seq + 1) % 1000;
            my $offer_id = ((time() % 86400) * 1000) + $nms_offer_seq;
            # Queue only -- one transmission per player after the loop. Each send
            # carries the whole pending list, so sending per item meant N*M
            # ever-larger packets for N items and M players.
            $mc->QueueNMSLootOffer($killer->GetName(), $item_id, $iname, $offer_id, $charges);
            $queued{$mc->CharacterID()} = $mc;
            $sent++;
        }

        # The offers REPLACE the corpse item. Leaving it would let anyone who can
        # loot the corpse take the original on top of their own copy.
        #
        # $charges is exactly the quantity Corpse::GetLootEntries() reported for
        # THIS row, and Corpse::RemoveItemByID() counts quantity in CHARGES
        # (corpse.cpp:927, stack_size = charges > 1 ? charges : 1) -- so for a stack
        # of 20 it removes the whole row, and for a plain 1-charge row it removes
        # that one row. Guessing 1 instead would leave 19 arrows lootable.
        #
        # Only after at least one recipient actually queued: if nobody qualified,
        # the corpse must stay untouched or the items would vanish.
        $corpse->RemoveItemByID($item_id, $charges) if ($sent);
        $n++;
    }

    # One packet per player carrying everything queued above.
    foreach my $mc (values %queued) {
        $mc->SendNMSLootPending();
    }
}

sub EVENT_DEATH_COMPLETE {
    plugin::CustomEventNPCDeathEntry($killer_id);

    if (defined($killed_corpse_id)) {
        my $corpse = $entity_list->GetCorpseByID($killed_corpse_id);
        if ($corpse) {        
            my %item_drops = (
                11703 => { #Box of Abu Kar 11703
                    'drop_chance' => 0.0001, # 1/1000% chance to drop
                    'min_level'   => 35, # Minimum level to drop from
                    'max_level'   => 99, # Maximum level to drop from
                },
                #2827 => { # Christmas Event
                #    'drop_chance' => 0.01,
                #    'min_level'   => 1, # Minimum level to drop from
                #    'max_level'   => 99, # Maximum level to drop from
                #},
                #56064 => { 
                #    'drop_chance' => 0.0005, # 5/1000% chance to drop
                #    'min_level'   => 1, # Minimum level to drop from
                #    'max_level'   => 99, # Maximum level to drop from
                #},
                #36013 => { 
                #    'drop_chance' => 0.0005, # 5/1000% chance to drop
                #    'min_level'   => 1, # Minimum level to drop from
                #    'max_level'   => 99, # Maximum level to drop from
                #}
                # ... more items and their attributes
            );

            for my $item_id (keys %item_drops) {
                if ($npc->GetLevel() >= $item_drops{$item_id}{'min_level'} && 
                    $npc->GetLevel() <= $item_drops{$item_id}{'max_level'}) {                    
                    if (rand() < $item_drops{$item_id}{'drop_chance'}) {
                        $corpse->AddItem($item_id, 1);
                        $corpse->SetDecayTimer(1500000);
                        quest::ding();
                    }
                }
            }
        }
    }

    # NMS-LOCAL: sweep the corpse LAST, after every other death handler has had its
    # chance to add to it. The rare-drop block above runs at death time, so sharing
    # before it left the drop sitting on a corpse nobody looks at any more -- with
    # all normal loot arriving in the Pending window, players stop clicking corpses
    # entirely. Runs only when the sharedloot master bucket is exactly "1"; see the
    # fail-closed gate note above nms_share_corpse_loot().
    if (defined($killed_corpse_id)) {
        my $shared_corpse = $entity_list->GetCorpseByID($killed_corpse_id);
        nms_share_corpse_loot($shared_corpse, $killer_id) if ($shared_corpse);
    }
}

sub EVENT_AGGRO {
    if (plugin::IsNMS() && $instanceid) {
        my $expedition = quest::get_expedition();
        if ($expedition) {
            plugin::ScaleInstanceNPC($npc, $expedition->GetMemberCount());
        }
    }
}

sub EVENT_SIGNAL {
    if ($signal == 66666) {
        my $expedition = quest::get_expedition();
        if ($expedition) {
            plugin::ScaleInstanceNPC($npc, $expedition->GetMemberCount());
        }
    }
}

sub EVENT_ITEM {
    if (plugin::CustomEventHandinEntry()) {
        return 1;
    }
}

sub EVENT_SPAWN {
    if (plugin::CustomEventNPCSpawnEntry($npc)) {
        return;
    }

    
    if ($instanceversion > 0) {        
        if ($npc->GetName() =~ /Herald_of_the_Triumvirate/) {
            $npc->Depop(0);
        }
    }

    if (plugin::IsNMS() && $instanceid) {
        my $expedition = quest::get_expedition();
        if ($expedition) {
            plugin::ScaleInstanceNPC($npc, $expedition->GetMemberCount());
        }
    }
}

sub EVENT_DAMAGE_GIVEN 
{
    # Special aggro events for player pets; if they are not taunting then add their owner to any
    # mob that they attack's aggro list. If they are taunting, then give them some bonus aggro.
    if ($npc->IsPet() && $npc->GetOwner()->IsClient()) {
       
        if ($npc->IsTaunting()) {
            my $ent = $entity_list->GetMobByID($entity_id);
            if ($ent) {
                $ent->AddToHateList($npc->GetOwner())
            }
        } else {
            my $ent = $entity_list->GetMobByID($entity_id);
            if ($ent) {
                $ent->AddToHateList($npc, 100);
            }
        }
    }
}

sub EVENT_KILLED_MERIT {
    plugin::ProcessSlayerCredit($client, $npc, $entity_list);
}
