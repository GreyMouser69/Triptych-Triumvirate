#include "../common/global_define.h"
#include "../common/data_verification.h"

#include "../common/loot.h"
#include "client.h"
#include "entity.h"
#include "mob.h"
#include "npc.h"
#include "zonedb.h"
#include "global_loot_manager.h"
#include "../common/repositories/criteria/content_filter_criteria.h"
#include "../common/repositories/global_loot_repository.h"
#include "quest_parser_collection.h"

#ifdef _WINDOWS
#define snprintf	_snprintf
#endif

void NPC::AddLootTable(uint32 loottable_id, bool is_global)
{
	// check if it's a GM spawn
	if (!npctype_id) {
		return;
	}

	if (m_resumed_from_zone_suspend) {
		LogZoneState("NPC [{}] is resuming from zone suspend, skipping", GetCleanName());
		LogLootDetail(
			"NPC [{}] type [{}] skipping loot-source capture for loottable [{}] global [{}] because it resumed from zone suspend",
			GetCleanName(),
			GetNPCTypeID(),
			loottable_id,
			is_global
		);
		return;
	}

	// NMS Fabled season: DoUpgradeLoot forces the Legendary tier only for the named's own table,
	// never for global loot. Scoped to this call; AddLootTable is synchronous.
	m_loading_global_loot = is_global;

	if (!is_global) {
		m_loot_copper   = 0;
		m_loot_silver   = 0;
		m_loot_gold     = 0;
		m_loot_platinum = 0;
	}

	zone->LoadLootTable(loottable_id);

	const auto *l = zone->GetLootTable(loottable_id);
	if (!l) {
		m_loading_global_loot = false;
		return;
	}

	LogLootDetail(
		"Attempting to load loot [{}] loottable [{}] ({}) is_global [{}]",
		GetCleanName(),
		loottable_id,
		l->name,
		is_global
	);

	auto content_flags = ContentFlags{
		.min_expansion = l->min_expansion,
		.max_expansion = l->max_expansion,
		.content_flags = l->content_flags,
		.content_flags_disabled = l->content_flags_disabled
	};

	if (!WorldContentService::Instance()->DoesPassContentFiltering(content_flags)) {
		m_loading_global_loot = false;
		return;
	}

	// NMS personal loot (phase B2): record the source now that every early-out has
	// passed and this table is guaranteed to actually be rolled. Placed here, not at
	// function entry, so a table that is filtered out or skipped is never captured.
	m_loot_source_tables.push_back({loottable_id, is_global});
	LogLootDetail(
		"NPC [{}] type [{}] captured loot-source table [{}] global [{}] source_history_size [{}]",
		GetCleanName(),
		GetNPCTypeID(),
		loottable_id,
		is_global,
		m_loot_source_tables.size()
	);

	uint32 min_cash = l->mincash;
	uint32 max_cash = l->maxcash;
	if (min_cash > max_cash) {
		const uint32 t = min_cash;
		min_cash = max_cash;
		max_cash = t;
	}

	uint32 cash = 0;
	if (!is_global) {
		if (max_cash > 0 && l->avgcoin > 0 && EQ::ValueWithin(l->avgcoin, min_cash, max_cash)) {
			const float upper_chance  = static_cast<float>(l->avgcoin - min_cash) /
										static_cast<float>(max_cash - min_cash);
			const float avg_cash_roll = static_cast<float>(zone->random.Real(0.0, 1.0));

			if (avg_cash_roll < upper_chance) {
				cash = zone->random.Int(l->avgcoin, max_cash);
			}
			else {
				cash = zone->random.Int(min_cash, l->avgcoin);
			}
		}
		else {
			cash = zone->random.Int(min_cash, max_cash);
		}
	}

	if (cash != 0) {
		m_loot_platinum = cash / 1000;
		cash -= m_loot_platinum * 1000;

		m_loot_gold = cash / 100;
		cash -= m_loot_gold * 100;

		m_loot_silver = cash / 10;
		cash -= m_loot_silver * 10;

		m_loot_copper = cash;
	}

	const uint32 global_loot_multiplier = RuleI(Zone, GlobalLootMultiplier);
	for (auto    &lte: zone->GetLootTableEntries(loottable_id)) {
		for (uint32 k = 1; k <= (lte.multiplier * global_loot_multiplier); k++) {
			const uint8 drop_limit   = lte.droplimit;
			const uint8 minimum_drop = lte.mindrop;
			const float probability  = lte.probability;

			float drop_chance = 0.0f;
			if (EQ::ValueWithin(probability, 0.0f, 100.0f)) {
				drop_chance = static_cast<float>(zone->random.Real(0.0, 100.0));
			}

			if (probability != 0.0 && (probability == 100.0 || drop_chance <= probability)) {
				AddLootDropTable(lte.lootdrop_id, drop_limit, minimum_drop);
			}
		}
	}

	LogLootDetail(
		"Loaded [{}] Loot Table [{}] is_global [{}]",
		GetCleanName(),
		loottable_id,
		is_global
	);

	m_loading_global_loot = false;
}

/*
 * NMS-LOCAL (Triptych): loot SELECTION, extracted from AddLootDropTable.
 *
 * These two traversals decide WHICH rows come out of a lootdrop; they create
 * nothing. Everything that turns a selected row into a real item -- allocating
 * the LootItem, upgrading the tier, equipping the NPC, firing EVENT_LOOT_ADDED,
 * recording roll statistics, updating the equipment light -- stays in
 * AddLootDrop()/AddLootDropFixed() or in the caller.
 *
 * WHY A CALLBACK AND NOT A RETURNED VECTOR
 * ---------------------------------------
 * AddLootDrop() can call DoUpgradeLoot(), which consumes zone->random. The
 * existing stream therefore interleaves selection and materialization:
 *
 *     chance roll -> AddLootDrop -> (maybe upgrade roll) -> next chance roll
 *
 * Collecting selections first and materializing afterwards would reorder that
 * stream (all selection rolls, then all upgrade rolls) and change which items
 * drop. on_selected is invoked SYNCHRONOUSLY, at exactly the points where the
 * previous code called AddLootDrop, so the draw order is unchanged.
 *
 * The callbacks are templates rather than std::function so they inline into the
 * selection loops: no type erasure, no indirect branch.
 *
 * meets_level is injected rather than called directly because the two paths use
 * it differently -- Path A asks for the verbose form (which logs every
 * rejection), Path B uses the non-verbose default. That asymmetry is preserved
 * deliberately.
 */

template <typename MeetsLevelFn, typename OnSelectedFn>
static void RollLootDropTableIndependent(
	const std::vector<LootdropEntriesRepository::LootdropEntries> &le,
	EQ::Random &rng,
	MeetsLevelFn &&meets_level,
	OnSelectedFn &&on_selected)
{
	// Flat scan: every entry gets `multiplier` independent chances. No weighting,
	// no droplimit, no mindrop. The level check runs AFTER the roll, so the roll
	// is consumed even when the entry is rejected -- moving it would shift the
	// stream.
	for (const auto &e: le) {
		for (int j = 0; j < e.multiplier; ++j) {
			if (rng.Real(0.0, 100.0) <= e.chance && meets_level(e, true)) {
				// Passed through unchanged, including a null result. The previous
				// code dereferenced it in both AddLootDrop and the log below; that
				// hazard is pre-existing and is NOT fixed here.
				const EQ::ItemData *database_item = database.GetItem(e.item_id);
				on_selected(e, database_item);
			}
		}
	}
}

/*
 * Weighted selection. Returns false only when no entry survives the
 * item-exists + level filter, which is the case that made the previous code
 * return early and SKIP UpdateEquipmentLight(). Returns true when the traversal
 * runs to completion, i.e. when the previous code would have reached it.
 */
template <typename MeetsLevelFn, typename OnSelectedFn>
static bool RollLootDropTableWeighted(
	const std::vector<LootdropEntriesRepository::LootdropEntries> &le,
	uint8 drop_limit,
	uint8 min_drop,
	EQ::Random &rng,
	MeetsLevelFn &&meets_level,
	OnSelectedFn &&on_selected)
{
	float roll_t                   = 0.0f;
	float no_loot_prob             = 1.0f;
	bool  roll_table_chance_bypass = false;
	bool  active_item_list         = false;

	// Pass 1 consumes no randomness: pure arithmetic over the filtered entries.
	for (const auto &e: le) {
		const EQ::ItemData *db_item = database.GetItem(e.item_id);
		if (db_item && meets_level(e, false)) {
			roll_t += e.chance;

			if (e.chance >= 100) {
				roll_table_chance_bypass = true;
			}
			else {
				no_loot_prob *= (100 - e.chance) / 100.0f;
			}

			active_item_list = true;
		}
	}

	if (!active_item_list) {
		return false;
	}

	// This will pick one item per iteration until mindrop.
	// Don't let the compare against chance fool you.
	// The roll isn't 0-100, its 0-total and it picks the item, we're just
	// looping to find the lucky item, descremening otherwise. This is ok,
	// items with chance 60 are 6 times more likely than items chance 10.
	int drops = 0;

	// translate above for loop using l and le
	for (int i = 0; i < drop_limit; ++i) {
		// The || short-circuits: while min_drop is unmet, or while a chance>=100
		// entry bypasses the gate, Real(0,1) is NOT drawn. Preserve that.
		if (drops < min_drop || roll_table_chance_bypass || (float) rng.Real(0.0, 1.0) >= no_loot_prob) {
			float           roll = (float) rng.Real(0.0, roll_t);
			for (const auto &e: le) {
				const auto *db_item = database.GetItem(e.item_id);
				if (db_item) {
					// if it doesn't meet the requirements do nothing
					if (!meets_level(e, false)) {
						continue;
					}

					if (roll < e.chance) {
						// Synchronous: any upgrade roll happens inside this call,
						// i.e. BEFORE the extra-copy roll below.
						on_selected(e, db_item);
						drops++;

						uint8 charges = e.multiplier;
						charges = EQ::ClampLower(charges, static_cast<uint8>(1));

						for (int k = 1; k < charges; ++k) {
							float c_roll = static_cast<float>(rng.Real(0.0, 100.0));
							if (c_roll <= e.chance) {
								on_selected(e, db_item);
							}
						}

						break;
					}
					else {
						roll -= e.chance;
					}
				}
			}
		}
	}

	return true;
}

void NPC::AddLootDropTable(uint32 lootdrop_id, uint8 drop_limit, uint8 min_drop)
{
	const auto l  = zone->GetLootdrop(lootdrop_id);
	const auto le = zone->GetLootdropEntries(lootdrop_id);
	if (l.id == 0 || le.empty()) {
		return;
	}

	// Delegates to the NPC method so the verbose logging path keeps using
	// GetCleanName()/database.CreateItemLink() exactly as before.
	auto meets_level = [this](const LootdropEntriesRepository::LootdropEntries &e, bool verbose) {
		return MeetsLootDropLevelRequirements(e, verbose);
	};

	// if this lootdrop is droplimit=0 and mindrop 0, scan list once and return
	if (drop_limit == 0 && min_drop == 0) {
		RollLootDropTableIndependent(
			le, zone->random, meets_level,
			[this, lootdrop_id](const LootdropEntriesRepository::LootdropEntries &e, const EQ::ItemData *database_item) {
				AddLootDrop(database_item, e);
				LogLootDetail(
					"---- NPC (Rolled) [{}] Lootdrop [{}] Item [{}] ({}) Chance [{}] Multiplier [{}]",
					GetCleanName(),
					lootdrop_id,
					database_item->Name,
					e.item_id,
					e.chance,
					e.multiplier
				);
			});
		// No UpdateEquipmentLight() on this path, same as before.
		return;
	}

	if (le.size() > 100 && drop_limit == 0) {
		drop_limit = 10;
	}

	if (drop_limit < min_drop) {
		drop_limit = min_drop;
	}

	// No success log on this path, same as before.
	if (RollLootDropTableWeighted(
			le, drop_limit, min_drop, zone->random, meets_level,
			[this](const LootdropEntriesRepository::LootdropEntries &e, const EQ::ItemData *db_item) {
				AddLootDrop(db_item, e);
			})) {
		UpdateEquipmentLight();
	}
}

/*
 * NMS personal loot (phase B3): replay the source history B2 captured and return
 * the rolled items by value.
 *
 * WHAT THIS IS NOT
 * ----------------
 * It is not a second loot pipeline and it does not call AddLootTable(). Calling
 * that would clear and rewrite m_loot_copper/.../m_loot_platinum, push a fresh
 * entry onto m_loot_source_tables (recursion), allocate real items, equip the
 * NPC and fire packets. Everything here is read-only apart from zone->random.
 *
 * RNG FIDELITY IS THE WHOLE POINT
 * -------------------------------
 * The two B1 traversals above invoke on_selected at the exact point where the
 * original code called AddLootDrop, and AddLootDrop could call DoUpgradeLoot,
 * which consumes zone->random. So the normal stream interleaves:
 *
 *     selection roll -> upgrade roll -> next selection roll -> ...
 *
 * Collecting the selections first and upgrading afterwards would reorder that
 * stream and change which items come out. So the personal callback upgrades
 * INSIDE the callback, before the traversal draws again.
 *
 * The upgrade guard is copied verbatim from AddLootDrop; when it does not pass,
 * DoUpgradeLoot is not called and no upgrade RNG is consumed, which is exactly
 * what the normal path does.
 *
 * m_loading_global_loot is only consulted by DoUpgradeLoot, so it is scoped to
 * the DoUpgradeLoot call alone -- result push_back deliberately happens after
 * the scope closes, so a vector allocation can never leave the NPC in the
 * wrong context.
 *
 * Items only: no cash, no corpse, no m_loot_items, no equipment, no
 * EVENT_LOOT_ADDED, no m_rolled_items. Death-time upgrades (LootBuff, seasonal,
 * hardcore) are NOT applied here -- that is B4's job, at kill time.
 */
NPC::PersonalLootRoll NPC::RollPersonalLoot()
{
	PersonalLootRoll roll;

	// No captured history means we must not invent one. It is decided purely by
	// emptiness: a zone-suspend restored NPC lands here with an empty vector even
	// though it has physical loot, because AddLootTable() returns on
	// m_resumed_from_zone_suspend before the capture point -- but that is only one
	// way to end up empty, so the cause is deliberately NOT recorded. B4 treats
	// false as "personal reroll unavailable, use the native/physical fallback".
	roll.source_history_available = !m_loot_source_tables.empty();
	if (!roll.source_history_available) {
		return roll;
	}

	// Local scope guard. A local class inside a member function carries the same
	// access rights as the function, so this reaches the protected member
	// legally without exposing it, adding a setter, or granting friendship.
	struct LoadingGlobalLootScope {
		NPC &npc;
		bool prior;

		LoadingGlobalLootScope(NPC &n, bool is_global) : npc(n), prior(n.m_loading_global_loot) {
			npc.m_loading_global_loot = is_global;
		}

		~LoadingGlobalLootScope() { npc.m_loading_global_loot = prior; }
	};

	for (const auto &src: m_loot_source_tables) {
		// Same lookup AddLootTable() performs. Content filtering and
		// GlobalLootEntry::PassesRules() are deliberately NOT re-run: B2 captured
		// the already-resolved sources, and re-evaluating global rules now would
		// test against current zone/hot-zone state instead of spawn-time state.
		zone->LoadLootTable(src.loottable_id);

		const auto *l = zone->GetLootTable(src.loottable_id);
		if (!l) {
			continue;
		}

		const uint32 global_loot_multiplier = RuleI(Zone, GlobalLootMultiplier);

		// Item generation, mirroring AddLootTable()'s loop verbatim minus cash.
		for (auto    &lte: zone->GetLootTableEntries(src.loottable_id)) {
			for (uint32 k = 1; k <= (lte.multiplier * global_loot_multiplier); k++) {
				const uint8 drop_limit   = lte.droplimit;
				const uint8 minimum_drop = lte.mindrop;
				const float probability  = lte.probability;

				float drop_chance = 0.0f;
				if (EQ::ValueWithin(probability, 0.0f, 100.0f)) {
					drop_chance = static_cast<float>(zone->random.Real(0.0, 100.0));
				}

				if (probability != 0.0 && (probability == 100.0 || drop_chance <= probability)) {
					const auto ld = zone->GetLootdrop(lte.lootdrop_id);
					const auto le = zone->GetLootdropEntries(lte.lootdrop_id);
					if (ld.id == 0 || le.empty()) {
						continue;
					}

					auto meets_level = [this](const LootdropEntriesRepository::LootdropEntries &e, bool verbose) {
						return MeetsLootDropLevelRequirements(e, verbose);
					};

					auto on_selected = [&](const LootdropEntriesRepository::LootdropEntries &e,
					                       const EQ::ItemData *db_item) {
						// Intentional defensive divergence. The independent traversal
						// hands on_selected whatever database.GetItem() returned, null
						// included, and the normal path then dereferences it in
						// AddLootDrop and in the log line. A personal roll is a
						// read-only preview, so it skips a null instead of
						// reproducing the crash. The weighted path already filters
						// nulls before calling back, so this only affects the
						// independent path.
						if (!db_item) {
							return;
						}

						uint32 final_item_id = db_item->ID;

						// Verbatim from AddLootDrop: when this does not hold, the
						// normal path calls no DoUpgradeLoot and consumes no upgrade
						// RNG, so neither does this.
						if (RuleB(Custom, DoItemUpgrades) && final_item_id <= 1000000 && !IsPet()) {
							// Scoped to the upgrade call: m_loading_global_loot decides
							// whether DoUpgradeLoot forces the Fabled tier, and it must
							// reflect this source's own-table/global identity.
							const LoadingGlobalLootScope scope(*this, src.is_global);
							final_item_id = DoUpgradeLoot(final_item_id);
						}

						// After the scope above closed: allocating here cannot leave
						// the NPC in the wrong global-loot context.
						roll.items.push_back({
							.item_id             = final_item_id,
							.charges             = static_cast<uint16>(e.item_charges),
							.source_loottable_id = src.loottable_id,
							.source_is_global    = src.is_global,
							.source_lootdrop_id  = lte.lootdrop_id
						});
					};

					// Path selection uses the ORIGINAL droplimit/mindrop, exactly as
					// AddLootDropTable does; normalization belongs to the weighted
					// branch only and never feeds back into this decision.
					if (drop_limit == 0 && minimum_drop == 0) {
						RollLootDropTableIndependent(
							le, zone->random, meets_level, on_selected);
					}
					else {
						uint8 norm_drop_limit = drop_limit;

						if (le.size() > 100 && drop_limit == 0) {
							norm_drop_limit = 10;
						}

						if (norm_drop_limit < minimum_drop) {
							norm_drop_limit = minimum_drop;
						}

						RollLootDropTableWeighted(
							le, norm_drop_limit, minimum_drop, zone->random, meets_level, on_selected);
					}
				}
			}
		}
	}

	return roll;
}

/*
 * NMS personal loot (phase B4a): extract the death-time upgrade context that
 * NPC::Death() used to compute inline.
 *
 * ZERO BEHAVIOUR CHANGE. This is a pure read:
 *   - no RNG is consumed, so it is safe to call before the pass-1 loop
 *   - nothing is mutated
 *   - the group/raid walk is transcribed verbatim, including the original's
 *     unguarded CastToClient() on each member. Adding an IsClient() guard would
 *     be a behaviour change and is deliberately NOT done here; if that is ever a
 *     real crash it should be fixed deliberately, not smuggled into a refactor.
 *
 * The two flags are kept separate because the two death passes are separate:
 * pass 1 (LootBuff) and pass 2 (seasonal/hardcore combined) each call
 * DoUpgradeLoot() once per item, so an eligible item is upgraded TWICE.
 */
NPC::DeathLootUpgradeContext NPC::BuildDeathLootUpgradeContext(Mob* killer)
{
	DeathLootUpgradeContext ctx;

	// Pass 1 reads a global buff cache; it is independent of the killer.
	ctx.loot_buff_enabled = database.LootBuffEnabled();

	// Find if anyone who is destined to get loot rights is Seasonal or HC.
	if (killer && killer->IsClient()) {
		ctx.seasonal_killer = killer->CastToClient()->IsSeasonal();
		ctx.hardcore_killer = killer->CastToClient()->IsHardcore();

		if (killer->IsGrouped()) {
			Group* g = entity_list.GetGroupByClient(killer->CastToClient());
			if (g) {
				for (const auto &m : g->members) {
					if (m) {
						ctx.seasonal_killer = ctx.seasonal_killer || m->CastToClient()->IsSeasonal();
						ctx.hardcore_killer = ctx.hardcore_killer || m->CastToClient()->IsHardcore();
					}
				}
			}
		} else if (killer->IsRaidGrouped()) {
			Raid* r = entity_list.GetRaidByClient(killer->CastToClient());
			if (r) {
				for (const auto &m : r->members) {
					if (m.member) {
						ctx.seasonal_killer = ctx.seasonal_killer || m.member->IsSeasonal();
						ctx.hardcore_killer = ctx.hardcore_killer || m.member->IsHardcore();
					}
				}
			}
		}
	}

	return ctx;
}

/*
 * NMS personal loot (phase B4b): recipients entitled to an independent
 * personal table/global roll.
 *
 * PHASE A GATE, in the same order the Perl path applies it (global_npc.pl):
 *   1. a resolvable Client killer
 *   2. RAID OVERRIDE -- a killer inside a raid always uses Normal Loot,
 *      regardless of the rule
 *   3. Custom:UsePersonalLoot must be on
 * Only then are recipients collected.
 *
 * ORDERING IS PART OF THE CONTRACT. Every RollPersonalLoot() call advances the
 * SHARED zone->random stream, so the enumeration order decides which random
 * subsequence each player receives. These are therefore sorted by CharacterID
 * ascending, NOT by group slot:
 *
 *   - group slot order shifts whenever somebody joins, leaves or is promoted,
 *     which would silently change another player's loot;
 *   - CharacterID is immutable, globally unique and monotonic, so the same
 *     roster always produces the same assignment;
 *   - it cannot leak pointer or entity-list iteration order into the result.
 *
 * Eligibility mirrors the Perl filters exactly: Client members only, same zone
 * as the killer, and alive (HP > 0).
 */
std::vector<Client *> NPC::GetPersonalLootRecipients(Mob* killer)
{
	std::vector<Client *> recipients;

	if (!killer || !killer->IsClient()) {
		return recipients;
	}

	// Raid override first, exactly as the Perl gate does.
	Client *killer_client = killer->CastToClient();
	if (killer_client->GetRaid()) {
		return recipients;
	}

	if (!RuleB(Custom, UsePersonalLoot)) {
		return recipients;
	}

	const uint32 zone_id = killer_client->GetZoneID();

	if (killer_client->IsGrouped()) {
		Group *g = entity_list.GetGroupByClient(killer_client);
		if (g) {
			// GroupCount() + members[i] is the C++ equivalent of the Perl
			// `$group->GetMember($_) for (0 .. $group->GroupCount() - 1)`.
			for (int i = 0; i < g->GroupCount(); ++i) {
				// IsClient() BEFORE CastToClient(), mirroring the Perl
				// `next unless ($m && $m->IsClient())`.
				//
				// This ordering is not cosmetic. Entity::CastToClient() only
				// range-checks for null; its IsClient() guard sits behind
				// #ifdef _EQDEBUG, which is NOT defined in a release build. So
				// casting a bot, merc, pet or any other non-Client member would
				// static_cast it to Client* and the following GetZoneID()/
				// GetHP()/CharacterID() calls would dereference a bogus pointer.
				Mob *member = g->members[i];
				if (!member || !member->IsClient()) {
					continue;
				}

				Client *client = member->CastToClient();
				if (client->GetZoneID() != zone_id || client->GetHP() <= 0) {
					continue;
				}
				recipients.push_back(client);
			}
		}
	}
	else if (killer_client->GetHP() > 0) {
		recipients.push_back(killer_client);
	}

	std::sort(recipients.begin(), recipients.end(),
		[](const Client *a, const Client *b) {
			return a->CharacterID() < b->CharacterID();
		});

	return recipients;
}

/*
 * NMS personal loot (phase B4b): confirm every FINAL item id in a staged roll
 * resolves to real item data.
 *
 * This runs BEFORE RemoveTableGeneratedItems() on purpose. Once the physical
 * table rows are destroyed they cannot be brought back, so every staged id must
 * already be provably deliverable. Discovering an undeliverable id afterwards
 * would silently drop an item that the corpse could still have supplied.
 *
 * Pure read: no RNG, no mutation, no side effects.
 */
bool NPC::ValidatePersonalLootItems(const PersonalLootRoll &roll)
{
	for (const auto &item: roll.items) {
		if (!database.GetItem(item.item_id)) {
			return false;
		}
	}
	return true;
}

/*
 * NMS personal loot (phase B4b): death-time upgrade passes over personal
 * results. See the declaration in npc.h for why this is two passes and why
 * seasonal/hardcore stay combined.
 *
 * These draws happen AFTER the physical corpse passes have already consumed
 * theirs, which is intended: personal results are a second, independent roll,
 * not a replay of the corpse rows.
 */
void NPC::ApplyDeathUpgrades(std::vector<PersonalLootRollResult> &items, const DeathLootUpgradeContext &ctx)
{
	if (ctx.loot_buff_enabled) {
		for (auto &item: items) {
			item.item_id = DoUpgradeLoot(item.item_id);
		}
	}

	if (ctx.seasonal_or_hardcore()) {
		for (auto &item: items) {
			item.item_id = DoUpgradeLoot(item.item_id);
		}
	}
}

/*
 * NMS personal loot (phase B4b): drop the physical table/global rows that the
 * personal rolls replace, keeping script/dynamic rows.
 *
 * Only ever called once EVERY recipient has been staged, so an abandoned
 * attempt leaves the corpse completely untouched.
 */
void NPC::RemoveTableGeneratedItems(LootItems &items)
{
	for (auto iter = items.begin(); iter != items.end();) {
		LootItem *item = *iter;
		if (item && item->lootdrop_id != 0) {
			iter = items.erase(iter);
			safe_delete(item);
			continue;
		}
		++iter;
	}
}

bool NPC::MeetsLootDropLevelRequirements(LootdropEntriesRepository::LootdropEntries loot_drop, bool verbose)
{
	if (loot_drop.npc_min_level > 0 && GetLevel() < loot_drop.npc_min_level) {
		if (verbose) {
			LogLootDetail(
				"NPC [{}] does not meet loot_drop level requirements (min_level) level [{}] current [{}] for item [{}]",
				GetCleanName(),
				loot_drop.npc_min_level,
				GetLevel(),
				database.CreateItemLink(loot_drop.item_id)
			);
		}
		return false;
	}

	if (loot_drop.npc_max_level > 0 && GetLevel() > loot_drop.npc_max_level) {
		if (verbose) {
			LogLootDetail(
				"NPC [{}] does not meet loot_drop level requirements (max_level) level [{}] current [{}] for item [{}]",
				GetCleanName(),
				loot_drop.npc_max_level,
				GetLevel(),
				database.CreateItemLink(loot_drop.item_id)
			);
		}
		return false;
	}

	return true;
}

uint32 NPC::DoUpgradeLoot(uint32 itemID) {
	// NMS Fabled season: a promoted named's own drops are forced to the season tier (Legendary).
	// Global loot keeps the normal roll. Items with no variant at that tier fall through below.
	if (IsFabled() && !m_loading_global_loot) {
		const uint32 base   = itemID % 1000000;
		const uint32 forced = base + static_cast<uint32>(GetFabledLootTier()) * 1000000;
		if (forced > itemID && database.GetItem(forced)) {
			return forced;
		}
	}

	if (RuleB(Custom, DoItemUpgrades)) {
		// roll MUST be a real, not an integer. Truncating it to uint32 bucketed every rate to a
		// whole percent: a configured 5.0 matched rolls 0-5 inclusive, which is 6%, and anything
		// below 1.0 (a 0.5% legendary, say) rounded up to a full 1%. The rates in the rules are
		// now the rates players actually get.
		double roll  = zone->random.Real(0.0, 100.0);
		uint32 newID = itemID % 1000000;

		// TODO: Affix system will need to update this
		uint32 currentTier = itemID / 1000000;

		if (roll <= RuleR(Custom, Tier2ItemDropRate) && currentTier < 2) {
			newID += 2000000;
		} else if (roll <= RuleR(Custom, Tier1ItemDropRate) && currentTier < 1) {
			newID += 1000000;
		}

		if (database.GetItem(newID) && newID > itemID) {
			itemID = newID;
		}
	}

	return itemID;
}

void NPC::AddLootDrop(
	const EQ::ItemData *item2,
	LootdropEntriesRepository::LootdropEntries loot_drop,
	bool wear_change,
	uint32 augment_one,
	uint32 augment_two,
	uint32 augment_three,
	uint32 augment_four,
	uint32 augment_five,
	uint32 augment_six
)
{
	if (m_resumed_from_zone_suspend) {
		LogZoneState("NPC [{}] is resuming from zone suspend, skipping", GetCleanName());
		return;
	}

	if (RuleB(Custom, DoItemUpgrades) && item2->ID <= 1000000 && !IsPet()) {
		item2 = database.GetItem(DoUpgradeLoot(item2->ID));
	}

	AddLootDropFixed(item2, loot_drop, wear_change, augment_one, augment_two, augment_three, augment_four, augment_five, augment_six);
}

//if itemlist is null, just send wear changes
void NPC::AddLootDropFixed(
	const EQ::ItemData *item2,
	LootdropEntriesRepository::LootdropEntries loot_drop,
	bool wear_change,
	uint32 augment_one,
	uint32 augment_two,
	uint32 augment_three,
	uint32 augment_four,
	uint32 augment_five,
	uint32 augment_six
)
{
	if (m_resumed_from_zone_suspend) {
		LogZoneState("NPC [{}] is resuming from zone suspend, skipping", GetCleanName());
		return;
	}

	if (!item2) {
		return;
	}

	auto item = new LootItem;

	if (EQEmuLogSys::Instance()->log_settings[Logs::Loot].is_category_enabled == 1) {
		EQ::SayLinkEngine linker;
		linker.SetLinkType(EQ::saylink::SayLinkItemData);
		linker.SetItemData(item2);

		LogLoot(
			"NPC [{}] Item ({}) [{}] charges [{}] chance [{}] trivial min/max [{}/{}] npc min/max [{}/{}]",
			GetName(),
			item2->ID,
			linker.GenerateLink(),
			loot_drop.item_charges,
			loot_drop.chance,
			loot_drop.trivial_min_level,
			loot_drop.trivial_max_level,
			loot_drop.npc_min_level,
			loot_drop.npc_max_level
		);
	}

	EQApplicationPacket *outapp               = nullptr;
	WearChange_Struct   *p_wear_change_struct = nullptr;
	if (wear_change) {
		outapp               = new EQApplicationPacket(OP_WearChange, sizeof(WearChange_Struct));
		p_wear_change_struct = (WearChange_Struct *) outapp->pBuffer;
		p_wear_change_struct->spawn_id = GetID();
		p_wear_change_struct->material = 0;
	}

	item->item_id           = item2->ID;
	item->charges           = loot_drop.item_charges;
	item->aug_1             = augment_one;
	item->aug_2             = augment_two;
	item->aug_3             = augment_three;
	item->aug_4             = augment_four;
	item->aug_5             = augment_five;
	item->aug_6             = augment_six;
	item->attuned           = false;
	item->trivial_min_level = loot_drop.trivial_min_level;
	item->trivial_max_level = loot_drop.trivial_max_level;
	item->equip_slot        = EQ::invslot::SLOT_INVALID;

	// unsure if required to equip, YOLO for now
	if (item2->ItemType == EQ::item::ItemTypeBow) {
		SetBowEquipped(true);
	}

	if (item2->ItemType == EQ::item::ItemTypeArrow) {
		SetArrowEquipped(true);
	}

	bool found = false; // track if we found an empty slot we fit into

	int found_slot = INVALID_INDEX; // for multi-slot items

	auto *inst = database.CreateItem(
		item2->ID,
		loot_drop.item_charges,
		augment_one,
		augment_two,
		augment_three,
		augment_four,
		augment_five,
		augment_six
	);

	if (!inst) {
		safe_delete(outapp);
		safe_delete(item);
		return;
	}

	if (loot_drop.equip_item > 0) {
		uint8              equipment_slot = UINT8_MAX;
		const EQ::ItemData *compitem      = nullptr;

		// Equip rules are as follows:
		// If the item has the NoPet flag set it will not be equipped.
		// An empty slot takes priority. The first empty one that an item can
		// fit into will be the one picked for the item.
		// AC is the primary choice for which item gets picked for a slot.
		// If AC is identical HP is considered next.
		// If an item can fit into multiple slots we'll pick the last one where
		// it is an improvement.

		if (item2->ItemType != EQ::item::ItemTypeAugmentation && !item2->NoPet) {
			std::vector<int> custom_order = {
				EQ::invslot::slotPrimary,
				EQ::invslot::slotSecondary,
				EQ::invslot::slotRange,
				EQ::invslot::slotCharm,
				EQ::invslot::slotEar1,
				EQ::invslot::slotHead,
				EQ::invslot::slotFace,
				EQ::invslot::slotEar2,
				EQ::invslot::slotNeck,
				EQ::invslot::slotShoulders,
				EQ::invslot::slotArms,
				EQ::invslot::slotBack,
				EQ::invslot::slotWrist1,
				EQ::invslot::slotWrist2,
				EQ::invslot::slotHands,
				EQ::invslot::slotFinger1,
				EQ::invslot::slotFinger2,
				EQ::invslot::slotChest,
				EQ::invslot::slotLegs,
				EQ::invslot::slotFeet,
				EQ::invslot::slotWaist
			};

			for (int i : custom_order) {
				if (found) break;

                // If we're checking the secondary slot, skip it if a 2h weapon is already equipped in primary.
                if (i == EQ::invslot::slotSecondary && equipment[EQ::invslot::slotPrimary] != 0) {
                    const EQ::ItemData *primary_item = database.GetItem(equipment[EQ::invslot::slotPrimary]);
                    if (primary_item && primary_item->IsType2HWeapon()) {
                        continue; // Skip equipping secondary if primary is a two-handed weapon
                    }
                }

				// skip it if something is already equipped in secondary.
				if (i == EQ::invslot::slotPrimary && equipment[EQ::invslot::slotSecondary] != 0) {
					if (item2 && item2->IsType2HWeapon()) {
						continue; // Skip equipping 2h weapon in primary if secondary has an item
					}
				}

				const uint32 slots = (1 << i);
				if (inst->GetItemSlots(true) & slots) {
					if (equipment[i]) {
						compitem = database.GetItem(equipment[i]);
						if (!compitem) {
							continue;
						}

						if (item2->AC > compitem->AC || (item2->AC == compitem->AC && item2->HP > compitem->HP)) {
							// item would be an upgrade
							if (item2->Slots != slots) {
								found_slot = i;
							}
							else {
								// Unequip old item
								auto *old_item = GetItem(i);
								if (!old_item) {
									continue;
								}

								old_item->equip_slot = EQ::invslot::SLOT_INVALID;

								equipment[i] = item2->ID;
								found_slot = i;
								found = true;
							}
						}
					} else {
						equipment[i] = item2->ID;
						found_slot = i;
						found = true;
					}
				}
			}
		}
		// Possible slot was found but not selected. Pick it now.
		if (!found && found_slot >= 0) {
			equipment[found_slot] = item2->ID;

			found = true;
		}

		uint32 equipment_material;
		if (
			item2->Material <= 0 ||
			(
				item2->Slots & (
					(1 << EQ::invslot::slotPrimary) |
					(1 << EQ::invslot::slotSecondary)
				)
			)
			) {
			equipment_material = Strings::ToUnsignedInt(&item2->IDFile[2]);
		}
		else {
			equipment_material = item2->Material;
		}

		if (found_slot == EQ::invslot::slotPrimary) {
			equipment_slot = EQ::textures::weaponPrimary;

			if (item2->Damage > 0) {
				SendAddPlayerState(PlayerState::PrimaryWeaponEquipped);

				if (!RuleB(Combat, ClassicNPCBackstab)) {
					SetFacestab(true);
				}
			}

			if (item2->IsType2HWeapon()) {
				if (!HasShieldEquipped()) {
					SetTwoHanderEquipped(true);
					equipment[EQ::invslot::slotSecondary] = 0;
					auto secondary = GetItem(EQ::invslot::slotSecondary);
					if (secondary) {
						secondary->equip_slot = EQ::invslot::SLOT_INVALID;
					}
				}
			}
		}
		else if (
			found_slot == EQ::invslot::slotSecondary &&
			(
				GetOwner() ||
				(CanThisClassDualWield() && zone->random.Roll(NPC_DW_CHANCE)) ||
				item2->Damage == 0
			) &&
			(
				item2->IsType1HWeapon() ||
				item2->ItemType == EQ::item::ItemTypeShield ||
				item2->ItemType == EQ::item::ItemTypeLight
			)
			) {
			if (!HasTwoHanderEquipped()) {
				equipment_slot = EQ::textures::weaponSecondary;

				if (item2->Damage > 0) {
					SendAddPlayerState(PlayerState::SecondaryWeaponEquipped);
				}
			} else {
				equipment[EQ::invslot::slotSecondary] = 0;
				auto secondary = GetItem(EQ::invslot::slotSecondary);
				if (secondary) {
					secondary->equip_slot = EQ::invslot::SLOT_INVALID;
				}
				found = false;
				found_slot = INVALID_INDEX;
			}
		}
		else if (found_slot == EQ::invslot::slotHead) {
			equipment_slot = EQ::textures::armorHead;
		}
		else if (found_slot == EQ::invslot::slotChest) {
			equipment_slot = EQ::textures::armorChest;
		}
		else if (found_slot == EQ::invslot::slotArms) {
			equipment_slot = EQ::textures::armorArms;
		}
		else if (EQ::ValueWithin(found_slot, EQ::invslot::slotWrist1, EQ::invslot::slotWrist2)) {
			equipment_slot = EQ::textures::armorWrist;
		}
		else if (found_slot == EQ::invslot::slotHands) {
			equipment_slot = EQ::textures::armorHands;
		}
		else if (found_slot == EQ::invslot::slotLegs) {
			equipment_slot = EQ::textures::armorLegs;
		}
		else if (found_slot == EQ::invslot::slotFeet) {
			equipment_slot = EQ::textures::armorFeet;
		}

		if (equipment_slot != UINT8_MAX) {
			if (wear_change) {
				p_wear_change_struct->wear_slot_id = equipment_slot;
				p_wear_change_struct->material     = equipment_material;
			}
		}

		if (found) {
			item->equip_slot = found_slot;
		}
	}

	if (found_slot != INVALID_INDEX) {
		GetInv().PutItem(found_slot, *inst);
	}

	if (parse->HasQuestSub(GetNPCTypeID(), EVENT_LOOT_ADDED)) {
		std::vector<std::any> args = {inst};
		parse->EventNPC(EVENT_LOOT_ADDED, this, nullptr, "", 0, &args);
	}

	item->lootdrop_id = loot_drop.lootdrop_id;
	m_loot_items.push_back(item);

	if (found) {
		CalcBonuses();
	}

	if (IsRecordLootStats()) {
		m_rolled_items.emplace_back(item->item_id);
	}

	//sync with Mob::GetEquipmentMaterial
	switch (GetRace()) {
		case Race::Wrulon:
		case Race::Phoenix:
		case Race::Spider:
		case Race::Worg:
			safe_delete(outapp);
			safe_delete(inst);
			return;
	}

	wear_change = wear_change && (IsPlayerRace(GetRace()) || (found_slot == EQ::invslot::slotPrimary || found_slot == EQ::invslot::slotSecondary));

	if (wear_change && outapp) {
		entity_list.QueueClients(this, outapp);
	}

	UpdateEquipmentLight();

	if (UpdateActiveLight()) {
		SendAppearancePacket(AppearanceType::Light, GetActiveLightType());
	}

	safe_delete(outapp);
	safe_delete(inst);
}

void NPC::AddItem(const EQ::ItemData *item, uint16 charges, bool equip_item)
{
	auto l = LootdropEntriesRepository::NewNpcEntity();

	l.equip_item   = static_cast<uint8>(equip_item ? 1 : 0);
	l.item_charges = charges;

	AddLootDropFixed(item, l, true);
}

void NPC::AddItem(
	uint32 item_id,
	uint16 charges,
	bool equip_item,
	uint32 augment_one,
	uint32 augment_two,
	uint32 augment_three,
	uint32 augment_four,
	uint32 augment_five,
	uint32 augment_six
)
{
	const auto *item = database.GetItem(item_id);
	if (!item) {
		return;
	}

	auto l = LootdropEntriesRepository::NewNpcEntity();

	l.equip_item   = static_cast<uint8>(equip_item ? 1 : 0);
	l.item_charges = charges;

	AddLootDropFixed(
		item,
		l,
		true,
		augment_one,
		augment_two,
		augment_three,
		augment_four,
		augment_five,
		augment_six
	);
}

void NPC::AddItemFixed(
	uint32 item_id,
	uint16 charges,
	bool equip_item,
	uint32 augment_one,
	uint32 augment_two,
	uint32 augment_three,
	uint32 augment_four,
	uint32 augment_five,
	uint32 augment_six
)
{
	const auto *item = database.GetItem(item_id);
	if (!item) {
		return;
	}

	auto l = LootdropEntriesRepository::NewNpcEntity();

	l.equip_item   = static_cast<uint8>(equip_item ? 1 : 0);
	l.item_charges = charges;

	AddLootDropFixed(
		item,
		l,
		true,
		augment_one,
		augment_two,
		augment_three,
		augment_four,
		augment_five,
		augment_six
	);
}

void NPC::AddLootTable()
{
	AddLootTable(m_loottable_id);
}

void NPC::CheckGlobalLootTables()
{
	const auto &l = zone->GetGlobalLootTables(this);
	for (const auto &e: l) {
		AddLootTable(e, true);
	}
}

void ZoneDatabase::LoadGlobalLoot()
{
	const auto &l = GlobalLootRepository::GetWhere(
		*this,
		fmt::format(
			"`enabled` = 1 {}",
			ContentFilterCriteria::apply()
		)
	);

	if (l.empty()) {
		return;
	}

	LogInfo(
		"Loaded [{}] Global Loot Entr{}.",
		Strings::Commify(l.size()),
		l.size() != 1 ? "ies" : "y"
	);

	const std::string &zone_id = std::to_string(zone->GetZoneID());

	for (const auto &e: l) {
		if (!e.zone.empty()) {
			const auto &zones = Strings::Split(e.zone, "|");

			if (!Strings::Contains(zones, zone_id)) {
				continue;
			}
		}

		GlobalLootEntry gle(e.id, e.loottable_id, e.description);

		if (e.min_level) {
			gle.AddRule(GlobalLoot::RuleTypes::LevelMin, e.min_level);
		}

		if (e.max_level) {
			gle.AddRule(GlobalLoot::RuleTypes::LevelMax, e.max_level);
		}

		if (e.rare) {
			gle.AddRule(GlobalLoot::RuleTypes::Rare, e.rare);
		}

		if (e.raid) {
			gle.AddRule(GlobalLoot::RuleTypes::Raid, e.raid);
		}

		if (!e.race.empty()) {
			const auto &races = Strings::Split(e.race, "|");

			for (const auto &r: races) {
				gle.AddRule(GlobalLoot::RuleTypes::Race, Strings::ToInt(r));
			}
		}

		if (!e.class_.empty()) {
			const auto &classes = Strings::Split(e.class_, "|");

			for (const auto &c: classes) {
				gle.AddRule(GlobalLoot::RuleTypes::Class, Strings::ToInt(c));
			}
		}

		if (!e.bodytype.empty()) {
			const auto &bodytypes = Strings::Split(e.bodytype, "|");

			for (const auto &b: bodytypes) {
				gle.AddRule(GlobalLoot::RuleTypes::BodyType, Strings::ToInt(b));
			}
		}

		if (e.hot_zone) {
			gle.AddRule(GlobalLoot::RuleTypes::HotZone, e.hot_zone);
		}

		zone->AddGlobalLootEntry(gle);
	}
}


LootItem *NPC::GetItem(int slot_id)
{
	LootItems::iterator cur, end;
	cur = m_loot_items.begin();
	end = m_loot_items.end();
	for (; cur != end; ++cur) {
		LootItem *item = *cur;
		if (item && item->equip_slot == slot_id) {
			return item;
		}
	}
	return (nullptr);
}

void NPC::RemoveItem(uint32 item_id, uint16 quantity, uint16 slot)
{
	LootItems::iterator cur, end;
	cur = m_loot_items.begin();
	end = m_loot_items.end();
	for(; cur != end; ++cur) {
		LootItem *item = *cur;
		if (item->item_id == item_id && slot <= 0 && quantity <= 0) {
			m_loot_items.erase(cur);
			UpdateEquipmentLight();
			if (UpdateActiveLight()) { SendAppearancePacket(AppearanceType::Light, GetActiveLightType()); }
			if (item->equip_slot >= EQ::invslot::EQUIPMENT_BEGIN && item->equip_slot <= EQ::invslot::EQUIPMENT_END) {
				equipment[item->equip_slot] = 0;
				SendWearChange(EQ::InventoryProfile::CalcMaterialFromSlot(item->equip_slot));
				GetInv().DeleteItem(item->equip_slot);
			}
			CalcBonuses();
			safe_delete(item);
			return;
		}
		else if (item->item_id == item_id && item->equip_slot == slot && quantity >= 1) {
			if (item->charges <= quantity) {
				m_loot_items.erase(cur);
				UpdateEquipmentLight();
				if (UpdateActiveLight()) { SendAppearancePacket(AppearanceType::Light, GetActiveLightType()); }
				if (item->equip_slot >= EQ::invslot::EQUIPMENT_BEGIN && item->equip_slot <= EQ::invslot::EQUIPMENT_END) {
					equipment[item->equip_slot] = 0;
					SendWearChange(EQ::InventoryProfile::CalcMaterialFromSlot(item->equip_slot));
					GetInv().DeleteItem(item->equip_slot);
				}
				CalcBonuses();
				safe_delete(item);
			}
			else {
				item->charges -= quantity;
			}
			return;
		}
	}
}

void NPC::CheckTrivialMinMaxLevelDrop(Mob *killer)
{
	if (killer == nullptr || !killer->IsClient()) {
		return;
	}

	uint16 killer_level = killer->GetLevel();
	uint8  material;

	auto cur = m_loot_items.begin();
	while (cur != m_loot_items.end()) {
		if (!(*cur)) {
			return;
		}

		uint16 trivial_min_level     = (*cur)->trivial_min_level;
		uint16 trivial_max_level     = (*cur)->trivial_max_level;
		bool   fits_trivial_criteria = (
			(trivial_min_level > 0 && killer_level < trivial_min_level) ||
			(trivial_max_level > 0 && killer_level > trivial_max_level)
		);

		if (fits_trivial_criteria) {
			material = EQ::InventoryProfile::CalcMaterialFromSlot((*cur)->equip_slot);
			if (material != EQ::textures::materialInvalid) {
				SendWearChange(material);
			}

			cur = m_loot_items.erase(cur);
			continue;
		}
		++cur;
	}

	UpdateEquipmentLight();
	if (UpdateActiveLight()) {
		SendAppearancePacket(AppearanceType::Light, GetActiveLightType());
	}
}

void NPC::ClearLootItems()
{
	LootItems::iterator cur, end;
	cur = m_loot_items.begin();
	end = m_loot_items.end();
	for (; cur != end; ++cur) {
		LootItem *item = *cur;
		safe_delete(item);
	}
	m_loot_items.clear();

	UpdateEquipmentLight();
	if (UpdateActiveLight()) {
		SendAppearancePacket(AppearanceType::Light, GetActiveLightType());
	}
}

void NPC::QueryLoot(Client *to, bool is_pet_query)
{
	if (!m_loot_items.empty()) {
		if (!is_pet_query) {
			to->Message(
				Chat::White,
				fmt::format(
					"Loot | {} ({}) ID: {} Loottable ID: {}",
					GetName(),
					GetID(),
					GetNPCTypeID(),
					GetLoottableID()
				).c_str()
			);
		}

		int item_count = 0;

		for (auto current_item: m_loot_items) {
			int item_number = (item_count + 1);
			if (!current_item) {
				LogError("ItemList error, null item.");
				continue;
			}

			if (!current_item->item_id || !database.GetItem(current_item->item_id)) {
				LogError("Database error, invalid item.");
				continue;
			}

			EQ::SayLinkEngine linker;
			linker.SetLinkType(EQ::saylink::SayLinkLootItem);
			linker.SetLootData(current_item);

			to->Message(
				Chat::White,
				fmt::format(
					"Item {} | {} ({}){}",
					item_number,
					linker.GenerateLink().c_str(),
					current_item->item_id,
					(
						current_item->charges > 1 ?
							fmt::format(
								" Amount: {}",
								current_item->charges
							) :
							""
					)
				).c_str()
			);
			item_count++;
		}
	}

	if (!is_pet_query) {
		if (m_loot_platinum || m_loot_gold || m_loot_silver || m_loot_copper) {
			to->Message(
				Chat::White,
				fmt::format(
					"Money | {}",
					Strings::Money(
						m_loot_platinum,
						m_loot_gold,
						m_loot_silver,
						m_loot_copper
					)
				).c_str()
			);
		}
	}
}

bool NPC::HasItem(uint32 item_id)
{
	if (!database.GetItem(item_id)) {
		return false;
	}

	for (auto loot_item: m_loot_items) {
		if (!loot_item) {
			LogError("NPC::HasItem() - ItemList error, null item");
			continue;
		}

		if (!loot_item->item_id || !database.GetItem(loot_item->item_id)) {
			LogError("NPC::HasItem() - Database error, invalid item");
			continue;
		}

		if (loot_item->item_id == item_id) {
			return true;
		}
	}
	return false;
}

uint32 NPC::CountItem(uint32 item_id)
{
	uint32 item_count = 0;
	if (!database.GetItem(item_id)) {
		return item_count;
	}

	for (auto loot_item: m_loot_items) {
		if (!loot_item) {
			LogError("NPC::CountItem() - ItemList error, null item");
			continue;
		}

		if (!loot_item->item_id || !database.GetItem(loot_item->item_id)) {
			LogError("NPC::CountItem() - Database error, invalid item");
			continue;
		}

		if (loot_item->item_id == item_id) {
			item_count += loot_item->charges > 0 ? loot_item->charges : 1;
		}
	}
	return item_count;
}

uint32 NPC::GetLootItemIDBySlot(uint16 loot_slot)
{
	for (auto loot_item: m_loot_items) {
		if (loot_item->lootslot == loot_slot) {
			return loot_item->item_id;
		}
	}
	return 0;
}

uint16 NPC::GetFirstLootSlotByItemID(uint32 item_id)
{
	for (auto loot_item: m_loot_items) {
		if (loot_item->item_id == item_id) {
			return loot_item->lootslot;
		}
	}
	return 0;
}

void NPC::AddLootCash(
	uint32 in_copper,
	uint32 in_silver,
	uint32 in_gold,
	uint32 in_platinum
)
{
	m_loot_copper   = in_copper >= 0 ? in_copper : 0;
	m_loot_silver   = in_silver >= 0 ? in_silver : 0;
	m_loot_gold     = in_gold >= 0 ? in_gold : 0;
	m_loot_platinum = in_platinum >= 0 ? in_platinum : 0;
}

void NPC::RemoveLootCash()
{
	m_loot_copper   = 0;
	m_loot_silver   = 0;
	m_loot_gold     = 0;
	m_loot_platinum = 0;
}
