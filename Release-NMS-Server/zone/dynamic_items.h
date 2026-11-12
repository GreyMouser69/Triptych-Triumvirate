#ifndef ZONE_DYNAMIC_ITEMS_H
#define ZONE_DYNAMIC_ITEMS_H

#include "../common/types.h"
#include "../common/dynamic_item_structs.h"

#include <string>
#include <unordered_map>
#include <vector>

class Client;
class Database;
struct StatBonuses;

namespace EQ {
	class ItemInstance;
}

// One row of dynamic_items: an item that rolls stats when bought, and the class pool it rolls from
struct DynamicItemDef {
	uint32 item_id         = 0;
	uint8  class_id        = 0;
	uint8  tier            = 1;
	uint8  min_affixes     = 1;
	uint8  max_affixes     = 2;
	float  stat_multiplier = 1.0f;
};

// One row of dynamic_affix_pool: a stat line a class can roll
struct DynamicAffixDef {
	EQ::eDynamicAffixID affix      = EQ::AFFIX_NONE;
	bool                guaranteed = false;
	uint32              weight     = 0;
	int32               min_value  = 0;
	int32               max_value  = 0;
	uint32              slot_mask  = 0; // 0 = any slot
};

// One stat line on a rolled item
struct RolledAffix {
	EQ::eDynamicAffixID affix      = EQ::AFFIX_NONE;
	int32               value      = 0;
	bool                guaranteed = false;
	double              position   = 0.0; // where the roll landed in its range, 0 = min, 1 = max
	bool                reforged   = false;
};

// #reforge: one random line of a rolled item replaced by another. Only one line per item can ever be
// reforged (the first one chosen). Stored in custom_data dyn_ench as "from|to|seed|count" (affix codes).
struct DynamicEnchant {
	EQ::eDynamicAffixID from  = EQ::AFFIX_NONE; // the line as the seed rolled it
	EQ::eDynamicAffixID to    = EQ::AFFIX_NONE; // what it is now; AFFIX_NONE = unchanged
	uint64_t            seed  = 0;              // the value roll for `to`
	uint32              count = 0;              // reforges paid for on this item
};

// Defined in dynamic_seed_parser.cpp (no database or zone dependencies, so it can be tested alone)
uint64_t GenerateNewItemSeed();
std::vector<RolledAffix> RollDynamicItem(
	uint64_t seed,
	const DynamicItemDef &def,
	const std::vector<DynamicAffixDef> &pool,
	uint32 item_slots
);

// Replaces the reforged line, if it's still on the item and its replacement is still in the pool
void ApplyDynamicEnchant(
	std::vector<RolledAffix> &rolled,
	const DynamicEnchant &enchant,
	const DynamicItemDef &def,
	const std::vector<DynamicAffixDef> &pool,
	uint32 item_slots
);

// Up to `count` replacements for a line: random affixes this item can roll, other than `taken`
// (the item's other lines). Each comes with its value; picking one stores `seed` as the enchant seed.
// `taken` must list every affix currently on the item EXCEPT the one being
// reforged (`from`); RollReforgeOptions excludes those affixes by value, so
// callers are responsible for ensuring `rolled` has unique affixes (RollDynamicItem
// guarantees this) and for excluding exactly the reforged line.
std::vector<RolledAffix> RollReforgeOptions(
	uint64_t seed,
	const std::vector<EQ::eDynamicAffixID> &taken,
	const DynamicItemDef &def,
	const std::vector<DynamicAffixDef> &pool,
	uint32 item_slots,
	uint32 count
);

// One rolled item compared against another (e.g. a spare against what's equipped)
struct DynamicItemComparison {
	std::vector<std::string> lines;    // every stat line of the compared item, with its difference
	std::vector<std::string> changes;  // only the differences, compact, for chat
	int                      better = 0;
	int                      worse  = 0;
	std::string              standout; // the compared item's biggest random affix gain
};

class DynamicItemsManager {
public:
	static DynamicItemsManager *Instance()
	{
		static DynamicItemsManager instance;
		return &instance;
	}

	void SetContentDatabase(Database *db) { m_content_db = db; }
	bool Load();

	const DynamicItemDef *GetItem(uint32 item_id) const;
	const std::vector<DynamicAffixDef> *GetClassPool(uint8 class_id) const;
	bool IsDynamicItem(uint32 item_id) const { return m_items.contains(item_id); }

	// The seed is the only per-item data stored; it lives in the item's custom_data so it persists
	// through inventory, bank and corpse saves. 0 = not rolled.
	static uint64 GetSeed(const EQ::ItemInstance &inst);
	static void SetSeed(EQ::ItemInstance &inst, uint64 seed);

	// #reforge record (custom_data dyn_ench) and the choices paid for but not yet taken (dyn_offer:
	// "from|seed", so they survive zoning). Setting the enchant rebuilds the item's stats.
	static DynamicEnchant GetEnchant(const EQ::ItemInstance &inst);
	static void SetEnchant(EQ::ItemInstance &inst, const DynamicEnchant &enchant);
	static bool GetReforgeOffer(const EQ::ItemInstance &inst, EQ::eDynamicAffixID &from, uint64 &seed);
	static void SetReforgeOffer(EQ::ItemInstance &inst, EQ::eDynamicAffixID from, uint64 seed);
	static void ClearReforgeOffer(EQ::ItemInstance &inst);
	static constexpr const char *REFORGE_OFFER_KEY = "dyn_offer";
	static constexpr uint32 REFORGE_OPTIONS = 2;

	// The replacements a paid reforge offers for one line of this item
	std::vector<RolledAffix> GetReforgeOptions(const EQ::ItemInstance &inst, EQ::eDynamicAffixID from, uint64 seed) const;

	// The rolled lines for an item instance; empty if it isn't a dynamic item or hasn't been rolled
	std::vector<RolledAffix> Roll(const EQ::ItemInstance &inst) const;

	// ItemInstance hook (installed at zone boot): rebuilds the instance's item data with its rolled
	// flat stats (HP/Mana/Endurance/AC/heroics). Those then flow through the normal item stat code
	// and item packets like any other item stat.
	static void ApplyToInstance(EQ::ItemInstance &inst);

	// Percentage affixes have no item field; they're totalled per character for the combat/spell hooks.
	// Flat stats are ignored here (they're already on the item via ApplyToInstance).
	static void ApplyAffix(EQ::eDynamicAffixID affix, int32 value, EQ::DynamicItemBonuses *d);

	// Any affix applied straight to a character, for bonuses that don't come from an item (armor set
	// bonuses): flat stats go to b, percentage affixes to d. Must run before ApplySkillBonuses.
	static void ApplyCharacterAffix(EQ::eDynamicAffixID affix, int32 value, StatBonuses *b, EQ::DynamicItemBonuses *d);

	// Adds the character's dynamic skill % and ability damage % totals to their item bonuses.
	// Skill % raises the skill value itself (Client::GetSkill), which drives to-hit, damage and
	// avoidance chances. Runs after all items, since normal item skill mods are "highest wins".
	static void ApplySkillBonuses(const EQ::DynamicItemBonuses &d, StatBonuses *b);

	// Clamps each percent total to Custom:DynamicItemPctCap / DynamicItemCritChancePctCap. Runs once
	// after all items and set bonuses are totalled, before anything reads them.
	static void CapBonuses(EQ::DynamicItemBonuses &d);

	// Spell and song bonuses; these stack with focus effects and instrument mods
	static int32 GetSpellDamagePct(const EQ::DynamicItemBonuses &d, uint16 spell_id);
	static int32 GetSongModPct(const EQ::DynamicItemBonuses &d, int skill);

	// Display IDs: each dynamic copy is sent to clients under its own item ID, derived from its seed
	// (clients cache item data by ID). Nothing is stored; zones remember the ones they've built so the
	// few client packets that send an item ID back (links, item name, preview) can be resolved.
	static constexpr uint32 DISPLAY_ID_BASE = 0x1000000; // above every real item ID
	static bool IsDisplayId(uint32 id) { return id >= DISPLAY_ID_BASE; }
	bool ResolveDisplayId(uint32 display_id, uint32 &item_id, uint64 &seed, std::string &enchant) const;
	uint32 ResolveItemId(uint32 id) const; // real item ID for a display ID, otherwise unchanged

	// Equipment slot groups used to line up comparable items (paired slots like Ear/Wrist/Finger and
	// Primary/Secondary are one group)
	static int SlotGroupForSlots(uint32 item_slots);
	static int SlotGroupForEquipSlot(int16 slot_id);
	static int SlotGroupFromName(const std::string &name);
	static const char *SlotGroupName(int group);
	static int SlotGroupCount();

	// One stat line as players see it, e.g. "+503 HP" or "* +4% Parry"
	static std::string FormatLine(const RolledAffix &line);
	static DynamicItemComparison Compare(
		const std::vector<RolledAffix> &base,
		const std::vector<RolledAffix> &other,
		bool color
	);

	// Player-facing description of a rolled item: a header line, then one line per stat
	std::vector<std::string> Describe(const EQ::ItemInstance &inst) const;

	// Chat summary sent to the owner when they receive a newly rolled item
	void SendSummary(Client *c, const EQ::ItemInstance &inst) const;

	// Rebuilds rolled stats on every dynamic item a client carries (after the tables are reloaded)
	// and refreshes what the client sees. Returns how many items were updated.
	int RefreshClient(Client *c) const;

	// Alternate currency refunded when a dynamic item is sold back (Merchant:DynamicItemSellRefundPct)
	static uint32 GetSellRefund(uint32 full_cost);

	static EQ::eDynamicAffixID AffixFromCode(const std::string &code);
	static const char *AffixCode(EQ::eDynamicAffixID affix);
	static const char *AffixName(EQ::eDynamicAffixID affix);

private:
	Database *m_content_db = nullptr;

	std::unordered_map<uint32, DynamicItemDef>              m_items;
	std::unordered_map<uint8, std::vector<DynamicAffixDef>> m_class_pools;
	std::unordered_map<std::string, std::string>            m_suffixes; // affix code -> name suffix

	struct DisplayIdEntry {
		uint32      item_id;
		uint64      seed;
		std::string enchant; // custom_data dyn_ench
	};
	std::unordered_map<uint32, DisplayIdEntry> m_display_ids;

	std::string GetSuffix(EQ::eDynamicAffixID affix) const;
};

#endif // ZONE_DYNAMIC_ITEMS_H
