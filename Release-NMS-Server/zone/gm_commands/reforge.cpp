#include "../client.h"
#include "../command.h"
#include "../dynamic_items.h"
#include "../../common/rulesys.h"
#include "../../common/say_link.h"
#include "../../common/strings.h"

#include <algorithm>
#include <limits>

// #reforge: pay platinum and Chronal Shards to replace one random line of a dynamic item, Diablo
// enchanting style. Each paid reforge offers two replacements; take one or keep the current line.
// Only one line per item can ever be reforged, and each reforge of the same item costs more.
namespace {
	constexpr float REFORGE_RANGE = 30.0f;

	struct ReforgeCost {
		uint64 platinum = 0;
		uint32 currency = 0;
	};

	// Tier N costs (2N - 1) x the base, so the price follows the rift income of the tiers that unlock it.
	// Rule inputs are clamped so a misconfigured/negative rule can never produce a free or wrapped cost:
	//   - bases are clamped to >= 0 (a zero base yields a zero-cost reforge; document if that is undesired)
	//   - growth percent is clamped to >= 0
	//   - max percent is clamped to a floor of 100 (a configured cap below 100% is raised to 100%)
	// All intermediate products use 128-bit arithmetic and are clamped to the destination width to
	// avoid overflow on extreme rule values.
	ReforgeCost GetReforgeCost(const DynamicItemDef &def, uint32 count)
	{
		const uint64 tier_mult = 2ull * std::max<uint8>(def.tier, 1) - 1;

		const int plat_base_rule     = RuleI(Custom, ReforgePlatBase);
		const int currency_base_rule = RuleI(Custom, ReforgeCurrencyBase);
		const int growth_rule        = RuleI(Custom, ReforgeCostGrowthPct);
		const int max_pct_rule      = RuleI(Custom, ReforgeCostMaxPct);

		const uint64 plat_base     = (plat_base_rule > 0)     ? static_cast<uint64>(plat_base_rule)     : 0ull;
		const uint64 currency_base = (currency_base_rule > 0) ? static_cast<uint64>(currency_base_rule) : 0ull;
		const uint64 growth        = (growth_rule > 0)        ? static_cast<uint64>(growth_rule)        : 0ull;
		const uint64 max_pct       = (max_pct_rule > 100)     ? static_cast<uint64>(max_pct_rule)     : 100ull;

		// pct = min(100 + growth * count, max_pct); guard the growth*count product against overflow.
		__int128 growth_term = static_cast<__int128>(growth) * static_cast<__int128>(count);
		uint64 raw_pct = 100ull;
		if (growth_term > 0) {
			if (growth_term > static_cast<__int128>(std::numeric_limits<uint64>::max()) - 100) {
				raw_pct = std::numeric_limits<uint64>::max();
			}
			else {
				raw_pct = 100ull + static_cast<uint64>(growth_term);
			}
		}
		const uint64 pct = std::min<uint64>(raw_pct, max_pct);

		ReforgeCost cost;
		// platinum (64-bit storage)
		{
			__int128 prod = static_cast<__int128>(plat_base) * static_cast<__int128>(tier_mult) * static_cast<__int128>(pct);
			prod /= 100;
			if (prod < 0) prod = 0;
			if (prod > static_cast<__int128>(std::numeric_limits<uint64>::max())) prod = static_cast<__int128>(std::numeric_limits<uint64>::max());
			cost.platinum = static_cast<uint64>(prod);
		}
		// currency (stored as uint32 by the alternate-currency system; clamp to INT_MAX so the
		// signed deduction in PayForReforge can never wrap)
		{
			__int128 prod = static_cast<__int128>(currency_base) * static_cast<__int128>(tier_mult) * static_cast<__int128>(pct);
			prod /= 100;
			if (prod < 0) prod = 0;
			const __int128 cap = static_cast<__int128>(std::numeric_limits<int>::max());
			if (prod > cap) prod = cap;
			cost.currency = static_cast<uint32>(prod);
		}
		return cost;
	}

	std::string CostText(const ReforgeCost &cost)
	{
		return fmt::format("{} platinum and {} Chronal Shards", Strings::Commify(cost.platinum), Strings::Commify(cost.currency));
	}

	bool AtReforgeNPC(Client *c)
	{
		const int npc_id = RuleI(Custom, ReforgeNPCID);
		if (npc_id <= 0) {
			return true;
		}

		auto t = c->GetTarget();
		return t && t->IsNPC() && t->GetNPCTypeID() == static_cast<uint32>(npc_id) &&
			c->CalculateDistance(t) <= REFORGE_RANGE;
	}

	// Equipped items and the general inventory and its bags (not the cursor or the bank)
	bool IsReforgeSlot(int16 slot_id)
	{
		return (slot_id >= EQ::invslot::POSSESSIONS_BEGIN && slot_id < EQ::invslot::slotCursor) ||
			(slot_id >= EQ::invbag::GENERAL_BAGS_BEGIN && slot_id <= EQ::invbag::GENERAL_BAGS_END);
	}

	EQ::ItemInstance *GetReforgeItem(Client *c, int16 slot_id)
	{
		if (!IsReforgeSlot(slot_id)) {
			return nullptr;
		}

		auto inst = c->GetInv().GetItem(slot_id);
		return inst && DynamicItemsManager::Instance()->IsDynamicItem(inst->GetID()) ? inst : nullptr;
	}

	void SaveReforgedItem(Client *c, int16 slot_id, EQ::ItemInstance *inst)
	{
		database.SaveInventory(c->CharacterID(), inst, slot_id);
		c->SendItemPacket(slot_id, inst, ItemPacketTrade);
		c->CalcBonuses();
	}

	// The line of this item that's reforged now (or the seed's original line, if kept so far)
	EQ::eDynamicAffixID CurrentAffix(const DynamicEnchant &e)
	{
		return e.to != EQ::AFFIX_NONE ? e.to : e.from;
	}

	void ListItems(Client *c)
	{
		auto mgr   = DynamicItemsManager::Instance();
		int  found = 0;
		auto list  = [&](int16 slot_id) {
			auto inst = GetReforgeItem(c, slot_id);
			if (!inst) {
				return;
			}

			const auto def = mgr->GetItem(inst->GetUnscaledItem()->ID);
			const auto e   = DynamicItemsManager::GetEnchant(*inst);
			c->Message(
				Chat::White,
				"%s",
				fmt::format(
					"{} {} (Tier {}{})",
					Saylink::Create(fmt::format("#reforge {}", slot_id), true, "Reforge"),
					inst->GetItem()->Name,
					def ? def->tier : 0,
					e.count ? fmt::format(", reforged {}x", e.count) : ""
				).c_str()
			);
			found++;
		};

		for (int16 slot_id = EQ::invslot::POSSESSIONS_BEGIN; slot_id < EQ::invslot::slotCursor; ++slot_id) {
			list(slot_id);
		}

		for (int16 slot_id = EQ::invbag::GENERAL_BAGS_BEGIN; slot_id <= EQ::invbag::GENERAL_BAGS_END; ++slot_id) {
			list(slot_id);
		}

		if (!found) {
			c->Message(Chat::White, "You aren't carrying any dynamic items. Equip them or put them in your bags (not the bank).");
		}
	}

	void ShowOffer(Client *c, int16 slot_id, const EQ::ItemInstance &inst, EQ::eDynamicAffixID from, uint64 seed)
	{
		auto       mgr     = DynamicItemsManager::Instance();
		const auto e       = DynamicItemsManager::GetEnchant(inst);
		const auto current = e.from == from ? CurrentAffix(e) : from;
		const auto options = mgr->GetReforgeOptions(inst, from, seed);

		c->Message(Chat::Yellow, "%s", fmt::format("Reforging {}: choose one.", inst.GetItem()->Name).c_str());
		for (const auto &line : mgr->Roll(inst)) {
			if (line.affix == current) {
				c->Message(
					Chat::White,
					"%s",
					fmt::format("{} {} (current)", Saylink::Create(fmt::format("#reforge {} keep", slot_id), true, "Keep"), DynamicItemsManager::FormatLine(line)).c_str()
				);
			}
		}

		for (size_t i = 0; i < options.size(); ++i) {
			c->Message(
				Chat::White,
				"%s",
				fmt::format("{} {}", Saylink::Create(fmt::format("#reforge {} take {}", slot_id, i + 1), true, "Take"), DynamicItemsManager::FormatLine(options[i])).c_str()
			);
		}
	}

	void ShowItem(Client *c, int16 slot_id, const EQ::ItemInstance &inst)
	{
		auto       mgr = DynamicItemsManager::Instance();
		const auto def = mgr->GetItem(inst.GetUnscaledItem()->ID);
		if (!def) {
			return;
		}

		EQ::eDynamicAffixID from = EQ::AFFIX_NONE;
		uint64              seed = 0;
		if (DynamicItemsManager::GetReforgeOffer(inst, from, seed)) {
			ShowOffer(c, slot_id, inst, from, seed);
			return;
		}

		const auto e      = DynamicItemsManager::GetEnchant(inst);
		const auto locked = e.from != EQ::AFFIX_NONE ? CurrentAffix(e) : EQ::AFFIX_NONE;
		const auto rolled = mgr->Roll(inst);

		c->Message(
			Chat::Yellow,
			"%s",
			fmt::format("{}: the next reforge costs {}.", inst.GetItem()->Name, CostText(GetReforgeCost(*def, e.count))).c_str()
		);

		for (size_t i = 0; i < rolled.size(); ++i) {
			const auto &line     = rolled[i];
			const bool  can      = !line.guaranteed && (locked == EQ::AFFIX_NONE || line.affix == locked);
			const auto  link     = can ? Saylink::Create(fmt::format("#reforge {} {}", slot_id, i + 1), true, "Reforge") + " " : std::string();
			c->Message(
				Chat::White,
				"%s",
				fmt::format("{}{}{}", link, DynamicItemsManager::FormatLine(line), line.reforged ? " (reforged)" : "").c_str()
			);
		}

		c->Message(
			Chat::White,
			locked == EQ::AFFIX_NONE
				? "Only random (*) lines can be reforged, and once you reforge one, it's the only line on this item that can be reforged again."
				: "Only the line reforged before can be reforged on this item."
		);
	}

	void PayForReforge(Client *c, int16 slot_id, EQ::ItemInstance *inst, uint32 line_number)
	{
		auto       mgr = DynamicItemsManager::Instance();
		const auto def = mgr->GetItem(inst->GetUnscaledItem()->ID);
		if (!def) {
			return;
		}

		EQ::eDynamicAffixID pending_from = EQ::AFFIX_NONE;
		uint64              pending_seed = 0;
		if (DynamicItemsManager::GetReforgeOffer(*inst, pending_from, pending_seed)) {
			c->Message(Chat::Red, "Take one of the choices you already paid for, or keep the current line, first.");
			ShowOffer(c, slot_id, *inst, pending_from, pending_seed);
			return;
		}

		const auto rolled = mgr->Roll(*inst);
		if (line_number < 1 || line_number > rolled.size() || rolled[line_number - 1].guaranteed) {
			c->Message(Chat::Red, "That line can't be reforged.");
			return;
		}

		auto       e    = DynamicItemsManager::GetEnchant(*inst);
		const auto line = rolled[line_number - 1];
		if (e.from != EQ::AFFIX_NONE && line.affix != CurrentAffix(e)) {
			c->Message(Chat::Red, "Only the line reforged before can be reforged on this item.");
			return;
		}

		const auto cost        = GetReforgeCost(*def, e.count);

		// Validate currency_id derived from the signed rule before any cast/use.
		const int currency_rule = RuleI(Custom, ReforgeCurrencyID);
		if (currency_rule <= 0) {
			c->Message(Chat::Red, "Reforge currency is not configured.");
			return;
		}
		const uint32 currency_id = static_cast<uint32>(currency_rule);
		if (!zone->DoesAlternateCurrencyExist(currency_id)) {
			c->Message(Chat::Red, "Reforge currency %u does not exist.", currency_id);
			return;
		}

		if (c->GetAlternateCurrencyValue(currency_id) < cost.currency) {
			c->Message(Chat::Red, "%s", fmt::format("You need {} to reforge this.", CostText(cost)).c_str());
			return;
		}

		// Guard platinum*1000 against overflow before deducting.
		if (cost.platinum > (std::numeric_limits<uint64>::max() / 1000)) {
			c->Message(Chat::Red, "%s", fmt::format("You need {} to reforge this.", CostText(cost)).c_str());
			return;
		}

		if (!c->TakeMoneyFromPP(cost.platinum * 1000, true)) {
			c->Message(Chat::Red, "%s", fmt::format("You need {} to reforge this.", CostText(cost)).c_str());
			return;
		}

		if (cost.currency) {
			// cost.currency is clamped to INT_MAX in GetReforgeCost, so this signed
			// deduction can never wrap.
			c->AddAlternateCurrencyValue(currency_id, -static_cast<int>(cost.currency));
		}

		// The line is locked from the first paid reforge on, even if the current line is kept
		if (e.from == EQ::AFFIX_NONE) {
			e.from = line.affix;
		}
		e.count++;

		DynamicItemsManager::SetEnchant(*inst, e);
		DynamicItemsManager::SetReforgeOffer(*inst, e.from, GenerateNewItemSeed());
		SaveReforgedItem(c, slot_id, inst);

		LogInfo(
			"[{}] paid [{}] platinum and [{}] currency [{}] to reforge [{}] on [{}] (slot [{}], reforge [{}])",
			c->GetCleanName(), cost.platinum, cost.currency, currency_id, DynamicItemsManager::AffixCode(line.affix),
			inst->GetItem()->Name, slot_id, e.count
		);

		EQ::eDynamicAffixID from = EQ::AFFIX_NONE;
		uint64              seed = 0;
		DynamicItemsManager::GetReforgeOffer(*inst, from, seed);
		ShowOffer(c, slot_id, *inst, from, seed);
	}

	void Choose(Client *c, int16 slot_id, EQ::ItemInstance *inst, uint32 choice)
	{
		auto                mgr  = DynamicItemsManager::Instance();
		EQ::eDynamicAffixID from = EQ::AFFIX_NONE;
		uint64              seed = 0;
		if (!DynamicItemsManager::GetReforgeOffer(*inst, from, seed)) {
			c->Message(Chat::Red, "There's no reforge waiting on that item.");
			return;
		}

		if (choice > 0) {
			const auto options = mgr->GetReforgeOptions(*inst, from, seed);
			if (choice > options.size()) {
				c->Message(Chat::Red, "There's no such choice.");
				return;
			}

			auto e = DynamicItemsManager::GetEnchant(*inst);
			e.from = from;
			e.to   = options[choice - 1].affix;
			e.seed = seed;
			DynamicItemsManager::ClearReforgeOffer(*inst);
			DynamicItemsManager::SetEnchant(*inst, e); // rebuilds the item's stats
		}
		else {
			DynamicItemsManager::ClearReforgeOffer(*inst);
		}

		SaveReforgedItem(c, slot_id, inst);
		mgr->SendSummary(c, *inst);
	}
}

void command_reforge(Client *c, const Seperator *sep)
{
	if (sep->argnum == 0) {
		ListItems(c);
		return;
	}

	if (!sep->IsNumber(1)) {
		c->Message(Chat::White, "Usage: #reforge - list your dynamic items | #reforge [slot] - an item's lines and cost | #reforge [slot] [line] - pay to reforge a line | #reforge [slot] take [1-2] / keep");
		return;
	}

	const auto slot_id = static_cast<int16>(Strings::ToInt(sep->arg[1]));
	auto       inst    = GetReforgeItem(c, slot_id);
	if (!inst) {
		c->Message(Chat::Red, "There's no dynamic item in that slot. Type #reforge to list them.");
		return;
	}

	if (sep->argnum == 1) {
		ShowItem(c, slot_id, *inst);
		return;
	}

	// Looking is free anywhere; paying and choosing happen at the Artificer
	if (!AtReforgeNPC(c)) {
		c->Message(Chat::Red, "Target the Chronal Artificer in The Void, and stand close, to reforge.");
		return;
	}

	if (!strcasecmp(sep->arg[2], "keep")) {
		Choose(c, slot_id, inst, 0);
		return;
	}

	if (!strcasecmp(sep->arg[2], "take") && sep->IsNumber(3) && Strings::ToInt(sep->arg[3]) >= 1) {
		Choose(c, slot_id, inst, static_cast<uint32>(Strings::ToInt(sep->arg[3])));
		return;
	}

	if (sep->IsNumber(2)) {
		PayForReforge(c, slot_id, inst, static_cast<uint32>(std::max(Strings::ToInt(sep->arg[2]), 0)));
		return;
	}

	c->Message(Chat::White, "Usage: #reforge [slot] [line] | #reforge [slot] take [1-2] | #reforge [slot] keep");
}
