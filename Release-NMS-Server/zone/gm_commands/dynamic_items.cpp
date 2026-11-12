#include "../client.h"
#include "../command.h"
#include "../dialogue_window.h"
#include "../dynamic_items.h"
#include "../../common/say_link.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <numeric>
#include <tuple>

namespace {
	// Every slot a player can be carrying a dynamic item in, except the bank
	std::vector<int16> CarriedSlots()
	{
		std::vector<int16> slots;
		for (int16 slot_id = EQ::invslot::POSSESSIONS_BEGIN; slot_id <= EQ::invslot::POSSESSIONS_END; ++slot_id) {
			slots.push_back(slot_id);
		}

		for (int16 slot_id = EQ::invbag::GENERAL_BAGS_BEGIN; slot_id <= EQ::invbag::GENERAL_BAGS_END; ++slot_id) {
			slots.push_back(slot_id);
		}

		return slots;
	}

	void SendDynamicItemSubCommands(Client *c)
	{
		c->Message(Chat::White, "Usage: #dynitem give [Item ID] [Seed] - Give your target (or you) a rolled dynamic item, random seed if omitted");
		c->Message(Chat::White, "Usage: #dynitem info - List your target's (or your) dynamic items with slots and seeds");
		c->Message(Chat::White, "Usage: #dynitem reroll [Slot ID] - Give the item in that slot a new random seed");
		c->Message(Chat::White, "Usage: #dynitem setseed [Slot ID] [Seed] - Set the seed of the item in that slot");
		c->Message(Chat::White, "Usage: #dynitem reload - Reload the dynamic item and armor set tables and refresh players in this zone");
	}

	// Saves and resends an item whose seed was just changed in place
	void ResaveDynamicItem(Client *t, int16 slot_id, EQ::ItemInstance *inst)
	{
		if (slot_id == EQ::invslot::slotCursor) {
			auto s = t->GetInv().cursor_cbegin(), e = t->GetInv().cursor_cend();
			database.SaveCursor(t->CharacterID(), s, e);
		} else {
			database.SaveInventory(t->CharacterID(), inst, slot_id);
		}

		t->SendItemPacket(slot_id, inst, ItemPacketTrade);
		t->CalcBonuses();
	}
}

void command_dynitem(Client *c, const Seperator *sep)
{
	const uint16 arguments = sep->argnum;
	if (!arguments) {
		SendDynamicItemSubCommands(c);
		return;
	}

	Client *t = c;
	if (c->GetTarget() && c->GetTarget()->IsClient()) {
		t = c->GetTarget()->CastToClient();
	}

	auto mgr = DynamicItemsManager::Instance();

	if (!strcasecmp(sep->arg[1], "give")) {
		if (!sep->IsNumber(2)) {
			SendDynamicItemSubCommands(c);
			return;
		}

		const uint32 item_id = Strings::ToUnsignedInt(sep->arg[2]);
		if (!mgr->IsDynamicItem(item_id)) {
			c->Message(Chat::Red, fmt::format("Item {} is not in dynamic_items.", item_id).c_str());
			return;
		}

		uint64 seed = arguments >= 3 && sep->IsNumber(3) ? Strings::ToUnsignedBigInt(sep->arg[3]) : 0;
		if (seed == 0) {
			seed = GenerateNewItemSeed();
		}

		auto inst = database.CreateItem(item_id);
		if (!inst) {
			c->Message(Chat::Red, fmt::format("Item {} does not exist.", item_id).c_str());
			return;
		}

		DynamicItemsManager::SetSeed(*inst, seed);
		t->PushItemOnCursor(*inst, true);
		mgr->SendSummary(t, *inst);
		safe_delete(inst);

		if (t != c) {
			c->Message(Chat::White, "%s", fmt::format("Gave {} item {} with seed {}.", t->GetCleanName(), item_id, seed).c_str());
		}
		return;
	}

	if (!strcasecmp(sep->arg[1], "info")) {
		int found = 0;
		for (const int16 slot_id : CarriedSlots()) {
			const auto inst = t->GetInv().GetItem(slot_id);
			if (!inst || !mgr->IsDynamicItem(inst->GetID())) {
				continue;
			}

			const auto lines = mgr->Describe(*inst);
			if (lines.empty()) {
				continue;
			}

			c->Message(
				Chat::Yellow,
				"%s",
				fmt::format("Slot {} | {} | Seed {}", slot_id, lines.front(), DynamicItemsManager::GetSeed(*inst)).c_str()
			);
			c->Message(Chat::White, "%s", Strings::Join(std::vector<std::string>(lines.begin() + 1, lines.end()), ", ").c_str());
			found++;
		}

		if (!found) {
			c->Message(Chat::White, fmt::format("{} is not carrying any dynamic items.", t->GetCleanName()).c_str());
		}
		return;
	}

	if (!strcasecmp(sep->arg[1], "reroll") || !strcasecmp(sep->arg[1], "setseed")) {
		const bool is_setseed = !strcasecmp(sep->arg[1], "setseed");
		if (!sep->IsNumber(2) || (is_setseed && !sep->IsNumber(3))) {
			SendDynamicItemSubCommands(c);
			return;
		}

		const auto slot_id = static_cast<int16>(Strings::ToInt(sep->arg[2]));
		auto inst = t->GetInv().GetItem(slot_id);
		if (!inst || !mgr->IsDynamicItem(inst->GetID())) {
			c->Message(Chat::Red, fmt::format("There is no dynamic item in slot {}.", slot_id).c_str());
			return;
		}

		const uint64 seed = is_setseed ? Strings::ToUnsignedBigInt(sep->arg[3]) : GenerateNewItemSeed();
		if (seed == 0) {
			c->Message(Chat::Red, "Seed must not be 0.");
			return;
		}

		DynamicItemsManager::SetSeed(*inst, seed);
		ResaveDynamicItem(t, slot_id, inst);
		mgr->SendSummary(t, *inst);

		if (t != c) {
			c->Message(Chat::White, fmt::format("Set seed of {}'s slot {} to {}.", t->GetCleanName(), slot_id, seed).c_str());
		}
		return;
	}

	if (!strcasecmp(sep->arg[1], "reload")) {
		if (!mgr->Load()) {
			c->Message(Chat::Red, "Failed to reload dynamic item tables, see the zone log.");
			return;
		}

		// Armor sets too, since dynamic items are usually set pieces
		LoadArmorSetBonuses();

		int players = 0, items = 0;
		for (auto &[id, client] : entity_list.GetClientList()) {
			const int updated = mgr->RefreshClient(client);
			if (updated) {
				players++;
				items += updated;
			}
			client->CalcBonuses();
		}

		c->Message(
			Chat::White,
			fmt::format(
				"Dynamic item and armor set tables reloaded. Updated {} items on {} players in this zone (other zones need a restart or their own reload).",
				items,
				players
			).c_str()
		);
		return;
	}

	SendDynamicItemSubCommands(c);
}

namespace {
	// One dynamic item a player owns, numbered for #dyninfo compare
	struct DynInfoEntry {
		int                      number = 0;
		int16                    slot_id = 0;
		const EQ::ItemInstance   *inst = nullptr;
		std::string              where;
		int                      group = -1;
		bool                     equipped = false;
		std::vector<RolledAffix> rolls;
	};

	// Everything the player owns, in a stable order: worn, inventory, bags, bank, shared bank
	std::vector<DynInfoEntry> GatherDynamicItems(Client *c)
	{
		auto mgr = DynamicItemsManager::Instance();

		std::vector<std::pair<int16, const char *>> slots;
		for (int16 s = EQ::invslot::POSSESSIONS_BEGIN; s <= EQ::invslot::POSSESSIONS_END; ++s) {
			const bool worn = s >= EQ::invslot::EQUIPMENT_BEGIN && s <= EQ::invslot::EQUIPMENT_END;
			slots.emplace_back(s, worn ? "equipped" : (s == EQ::invslot::slotCursor ? "cursor" : "inventory"));
		}
		for (int16 s = EQ::invbag::GENERAL_BAGS_BEGIN; s <= EQ::invbag::GENERAL_BAGS_END; ++s) {
			slots.emplace_back(s, "bag");
		}
		for (int16 s = EQ::invslot::BANK_BEGIN; s <= EQ::invslot::BANK_END; ++s) {
			slots.emplace_back(s, "bank");
		}
		for (int16 s = EQ::invbag::BANK_BAGS_BEGIN; s <= EQ::invbag::BANK_BAGS_END; ++s) {
			slots.emplace_back(s, "bank");
		}
		for (int16 s = EQ::invslot::SHARED_BANK_BEGIN; s <= EQ::invslot::SHARED_BANK_END; ++s) {
			slots.emplace_back(s, "shared bank");
		}
		for (int16 s = EQ::invbag::SHARED_BANK_BAGS_BEGIN; s <= EQ::invbag::SHARED_BANK_BAGS_END; ++s) {
			slots.emplace_back(s, "shared bank");
		}

		std::vector<DynInfoEntry> entries;
		for (const auto &[slot_id, where] : slots) {
			const auto inst = c->GetInv().GetItem(slot_id);
			if (!inst || !mgr->IsDynamicItem(inst->GetID())) {
				continue;
			}

			DynInfoEntry e;
			e.number   = static_cast<int>(entries.size()) + 1;
			e.slot_id  = slot_id;
			e.inst     = inst;
			e.where    = where;
			e.equipped = slot_id >= EQ::invslot::EQUIPMENT_BEGIN && slot_id <= EQ::invslot::EQUIPMENT_END;
			e.group    = e.equipped
				? DynamicItemsManager::SlotGroupForEquipSlot(slot_id)
				: DynamicItemsManager::SlotGroupForSlots(inst->GetUnscaledItem()->Slots);
			e.rolls    = mgr->Roll(*inst);
			entries.push_back(std::move(e));
		}

		return entries;
	}

	// What a group's spares are compared against: the first dynamic item worn in that group
	const DynInfoEntry *GroupBaseline(const std::vector<DynInfoEntry> &entries, int group)
	{
		for (const auto &e : entries) {
			if (e.group == group && e.equipped) {
				return &e;
			}
		}

		return nullptr;
	}

	std::string EntryTitle(const DynInfoEntry &e)
	{
		return fmt::format("[{}] {} ({})", e.number, e.inst->GetItem()->Name, e.where);
	}

	// The popup drops anything over 4095 characters
	constexpr size_t POPUP_LIMIT = 4000;

	const DynInfoEntry *FindEntry(const std::vector<DynInfoEntry> &entries, int number)
	{
		for (const auto &e : entries) {
			if (e.number == number) {
				return &e;
			}
		}

		return nullptr;
	}

	// "Parry %" -> "Parry", is_pct = true
	std::string TableLabel(EQ::eDynamicAffixID affix, bool &is_pct)
	{
		std::string name = DynamicItemsManager::AffixName(affix);
		is_pct = name.size() > 2 && name.ends_with(" %");
		if (is_pct) {
			name.resize(name.size() - 2);
		}

		return name;
	}

	void SendPagedTable(
		Client *c,
		const std::string &title,
		const std::string &top,
		const std::string &header,
		const std::vector<std::string> &rows,
		const std::string &bottom,
		int page,
		const std::string &page_command
	);

	// One item's stats as a table, or a second item compared against it column by column. The popup holds
	// 4,095 characters (OnLevelMessage_Struct; longer text is dropped by the client), so a long table is split
	// into pages: page_command + " <page>" re-sends this view at that page (a clickable link in chat).
	void SendDynInfoTable(
		Client *c,
		const DynInfoEntry &first,
		const DynInfoEntry *second,
		const std::string &title,
		int page = 1,
		const std::string &page_command = ""
	)
	{
		std::vector<EQ::eDynamicAffixID> affixes;
		for (const auto *e : { &first, second }) {
			if (!e) {
				continue;
			}
			for (const auto &line : e->rolls) {
				if (std::find(affixes.begin(), affixes.end(), line.affix) == affixes.end()) {
					affixes.push_back(line.affix);
				}
			}
		}
		std::sort(affixes.begin(), affixes.end());

		auto find_line = [](const DynInfoEntry &e, EQ::eDynamicAffixID affix) -> const RolledAffix * {
			for (const auto &line : e.rolls) {
				if (line.affix == affix) {
					return &line;
				}
			}
			return nullptr;
		};

		auto header = DialogueWindow::TableCell("Stat") +
			DialogueWindow::TableCell(fmt::format("#{}{}", first.number, first.equipped ? " (worn)" : ""));
		if (second) {
			header += DialogueWindow::TableCell(fmt::format("#{}", second->number)) + DialogueWindow::TableCell("Change");
		}

		// Every row, with the totals counted over the whole comparison
		std::vector<std::string> rows;
		int                      better = 0, worse = 0;
		for (const auto affix : affixes) {
			bool       is_pct = false;
			const auto name   = TableLabel(affix, is_pct);
			const auto unit   = is_pct ? "%" : "";
			const auto a      = find_line(first, affix);
			const auto b      = second ? find_line(*second, affix) : nullptr;
			const bool random = (a && !a->guaranteed) || (b && !b->guaranteed);

			auto cell = [&](const RolledAffix *l) {
				return DialogueWindow::TableCell(l ? fmt::format("{}{}", l->value, unit) : "-");
			};

			std::string row = DialogueWindow::TableCell(fmt::format("{}{}", random ? "* " : "", name)) + cell(a);
			if (second) {
				const int diff = (b ? b->value : 0) - (a ? a->value : 0);
				std::string change;
				if (diff > 0) {
					better++;
					change = DialogueWindow::ColorMessage("green", fmt::format("{:+}{}", diff, unit));
				} else if (diff < 0) {
					worse++;
					change = DialogueWindow::ColorMessage("red", fmt::format("{:+}{}", diff, unit));
				}
				row += cell(b) + DialogueWindow::TableCell(change);
			}

			rows.push_back(DialogueWindow::TableRow(row));
		}

		std::string top = DialogueWindow::ColorMessage("gold", EntryTitle(first)) + DialogueWindow::Break();
		if (second) {
			top += "vs " + DialogueWindow::ColorMessage("white", EntryTitle(*second)) + DialogueWindow::Break();
		}

		std::string bottom;
		if (second) {
			bottom += fmt::format(
				"{} better, {} worse",
				DialogueWindow::ColorMessage("green", std::to_string(better)),
				DialogueWindow::ColorMessage("red", std::to_string(worse))
			) + DialogueWindow::Break();
		}
		bottom += "* = random affix";

		SendPagedTable(c, title, top, header, rows, bottom, page, page_command);
	}

	// A table popup split into pages that fit (top and bottom repeat on every page). page_command + " <page>"
	// re-sends the view at that page, offered as clickable links in chat.
	void SendPagedTable(
		Client *c,
		const std::string &title,
		const std::string &top,
		const std::string &header,
		const std::vector<std::string> &rows,
		const std::string &bottom,
		int page,
		const std::string &page_command
	)
	{
		// Split the rows into pages that fit, leaving room for the page line
		constexpr size_t popup_limit = 4095;
		const std::string page_line_room = DialogueWindow::Break() + "Page 99 of 99";
		const size_t      fixed = top.size() + DialogueWindow::Table(DialogueWindow::TableRow(header)).size() +
			bottom.size() + page_line_room.size();

		std::vector<std::pair<size_t, size_t>> pages; // [first row, end)
		size_t                                 begin = 0, size = fixed;
		for (size_t i = 0; i < rows.size(); ++i) {
			if (i > begin && size + rows[i].size() > popup_limit) {
				pages.emplace_back(begin, i);
				begin = i;
				size  = fixed;
			}
			size += rows[i].size();
		}
		pages.emplace_back(begin, rows.size());

		const int page_count = static_cast<int>(pages.size());
		page = std::clamp(page, 1, page_count);
		const auto [from, to] = pages[page - 1];

		std::string table_rows = DialogueWindow::TableRow(header);
		for (size_t i = from; i < to; ++i) {
			table_rows += rows[i];
		}

		std::string text = top + DialogueWindow::Table(table_rows) + bottom;
		if (page_count > 1) {
			text += DialogueWindow::Break() + fmt::format("Page {} of {}", page, page_count);
		}

		c->SendPopupToClient(
			(page_count > 1 ? fmt::format("{} ({}/{})", title, page, page_count) : title).c_str(),
			text.c_str()
		);

		if (page_count > 1 && !page_command.empty()) {
			std::vector<std::string> links;
			for (int p = 1; p <= page_count; ++p) {
				if (p != page) {
					links.push_back(Saylink::Create(fmt::format("{} {}", page_command, p), true, fmt::format("Page {}", p)));
				}
			}
			c->Message(
				Chat::White,
				"%s",
				fmt::format("{} has {} pages: {}", title, page_count, Strings::Join(links, " ")).c_str()
			);
		}
	}

	// Clickable links in chat, one per item in each slot, that open its comparison window
	void SendDynInfoLinks(Client *c, const std::vector<DynInfoEntry> &entries, int only_group = -1)
	{
		for (int group = 0; group < DynamicItemsManager::SlotGroupCount(); ++group) {
			if (only_group >= 0 && group != only_group) {
				continue;
			}

			std::vector<std::string> links;
			for (const auto &e : entries) {
				if (e.group != group) {
					continue;
				}
				links.push_back(Saylink::Create(fmt::format("#dyninfo {}", e.number), true, fmt::format("#{}", e.number)));
			}

			if (!links.empty()) {
				c->Message(
					Chat::White,
					"%s",
					fmt::format("{}: {}", DynamicItemsManager::SlotGroupName(group), Strings::Join(links, " ")).c_str()
				);
			}
		}
	}

	void SendDynInfoOverview(Client *c, const std::vector<DynInfoEntry> &entries)
	{
		std::string text;
		bool truncated = false;

		for (int group = 0; group < DynamicItemsManager::SlotGroupCount(); ++group) {
			const bool has_items = std::any_of(
				entries.begin(),
				entries.end(),
				[&](const DynInfoEntry &e) { return e.group == group; }
			);
			if (!has_items) {
				continue;
			}

			const auto  baseline = GroupBaseline(entries, group);
			std::string block    = DialogueWindow::ColorMessage("gold", Strings::ToUpper(DynamicItemsManager::SlotGroupName(group)));
			block += baseline ? fmt::format(" - {}", EntryTitle(*baseline)) : " - nothing dynamic equipped";
			block += DialogueWindow::Break();

			for (const auto &e : entries) {
				if (e.group != group || &e == baseline) {
					continue;
				}

				block += DialogueWindow::Indent() + EntryTitle(e);
				if (baseline) {
					const auto cmp = DynamicItemsManager::Compare(baseline->rolls, e.rolls, false);
					block += fmt::format(
						"  {} better, {} worse",
						DialogueWindow::ColorMessage("green", std::to_string(cmp.better)),
						DialogueWindow::ColorMessage("red", std::to_string(cmp.worse))
					);
					if (!cmp.standout.empty()) {
						block += ", " + cmp.standout;
					}
				}
				block += DialogueWindow::Break();
			}

			block += DialogueWindow::Break();

			if (text.size() + block.size() > POPUP_LIMIT) {
				truncated = true;
				break;
			}

			text += block;
		}

		if (truncated) {
			text += "Too many items to list; use #dyninfo [slot]." + DialogueWindow::Break();
		}

		text += "Click an item number in chat (or #dyninfo [#]) to compare it with what you're wearing. #dyninfo compare [#] [#] for any two. #dyninfo worn for your totals.";
		c->SendPopupToClient("Dynamic Items", text.c_str());
		SendDynInfoLinks(c, entries);
	}

	void SendDynInfoSlot(Client *c, const std::vector<DynInfoEntry> &entries, int group)
	{
		const auto  baseline = GroupBaseline(entries, group);
		std::string text;
		bool        any = false;

		auto add_block = [&](const std::string &block) {
			if (text.size() + block.size() > POPUP_LIMIT) {
				return false;
			}
			text += block;
			return true;
		};

		if (baseline) {
			std::string block = DialogueWindow::ColorMessage("gold", EntryTitle(*baseline)) + DialogueWindow::Break();
			for (const auto &line : baseline->rolls) {
				block += DialogueWindow::Indent() + DynamicItemsManager::FormatLine(line) + DialogueWindow::Break();
			}
			add_block(block + DialogueWindow::Break());
			any = true;
		}

		for (const auto &e : entries) {
			if (e.group != group || &e == baseline) {
				continue;
			}

			any = true;
			std::string block = DialogueWindow::ColorMessage("white", EntryTitle(e)) + DialogueWindow::Break();
			if (baseline) {
				for (const auto &line : DynamicItemsManager::Compare(baseline->rolls, e.rolls, true).lines) {
					block += DialogueWindow::Indent() + line + DialogueWindow::Break();
				}
			} else {
				for (const auto &line : e.rolls) {
					block += DialogueWindow::Indent() + DynamicItemsManager::FormatLine(line) + DialogueWindow::Break();
				}
			}

			if (!add_block(block + DialogueWindow::Break())) {
				text += "More items than fit; use #dyninfo compare [#] [#].";
				break;
			}
		}

		if (!any) {
			c->Message(
				Chat::White,
				"%s",
				fmt::format("You have no dynamic {} items.", DynamicItemsManager::SlotGroupName(group)).c_str()
			);
			return;
		}

		if (baseline) {
			text += "(differences are against your equipped item) * = random affix";
		}

		c->SendPopupToClient(fmt::format("Dynamic Items: {}", DynamicItemsManager::SlotGroupName(group)).c_str(), text.c_str());
		SendDynInfoLinks(c, entries, group);
	}

	void SendDynInfoCompare(Client *c, const std::vector<DynInfoEntry> &entries, int a, int b, int page = 1)
	{
		const auto first  = FindEntry(entries, a);
		const auto second = FindEntry(entries, b);
		if (!first || !second) {
			c->Message(Chat::Red, "Unknown item number. Type #dyninfo to see the numbers.");
			return;
		}

		SendDynInfoTable(c, *first, second, fmt::format("Compare #{} with #{}", b, a), page, fmt::format("#dyninfo compare {} {}", a, b));
	}

	// One item against whatever is worn in its slot (or on its own if it's the worn one)
	void SendDynInfoItem(Client *c, const std::vector<DynInfoEntry> &entries, int number, int page = 1)
	{
		const auto e = FindEntry(entries, number);
		if (!e) {
			c->Message(Chat::Red, "Unknown item number. Type #dyninfo to see the numbers.");
			return;
		}

		const auto worn = GroupBaseline(entries, e->group);
		if (!worn || worn == e) {
			SendDynInfoTable(c, *e, nullptr, fmt::format("Dynamic Item #{}", number), page, fmt::format("#dyninfo {}", number));
			return;
		}

		SendDynInfoTable(c, *worn, e, fmt::format("Compare #{} with worn #{}", number, worn->number), page, fmt::format("#dyninfo {}", number));
	}

	// armor_set_bonuses.stat_id (see Client::CheckArmorSetBonuses) as the matching affix, where there is one
	EQ::eDynamicAffixID SetStatAffix(int32 stat_id)
	{
		switch (stat_id) {
			case 1: return EQ::AFFIX_HP;
			case 2: return EQ::AFFIX_MANA;
			case 3: return EQ::AFFIX_AC;
			case 4: return EQ::AFFIX_ATTACK;
			case 5: return EQ::AFFIX_MANA_REGEN;
			case 6: return EQ::AFFIX_HP_REGEN;
			case 8: return EQ::AFFIX_DAMAGE_SHIELD;
			case 20: return EQ::AFFIX_STR;
			case 21: return EQ::AFFIX_STA;
			case 22: return EQ::AFFIX_DEX;
			case 23: return EQ::AFFIX_AGI;
			case 24: return EQ::AFFIX_WIS;
			case 25: return EQ::AFFIX_INT;
			case 26: return EQ::AFFIX_CHA;
			case 30: return EQ::AFFIX_HEROIC_STR;
			case 31: return EQ::AFFIX_HEROIC_STA;
			case 32: return EQ::AFFIX_HEROIC_DEX;
			case 33: return EQ::AFFIX_HEROIC_AGI;
			case 34: return EQ::AFFIX_HEROIC_WIS;
			case 35: return EQ::AFFIX_HEROIC_INT;
			case 60: return EQ::AFFIX_MAGIC_RESIST;
			case 61: return EQ::AFFIX_FIRE_RESIST;
			case 62: return EQ::AFFIX_COLD_RESIST;
			case 63: return EQ::AFFIX_POISON_RESIST;
			case 64: return EQ::AFFIX_DISEASE_RESIST;
			default: return EQ::AFFIX_NONE;
		}
	}

	// Set bonus stat_ids with no affix of their own
	const char *SetStatLabel(int32 stat_id)
	{
		switch (stat_id) {
			case 7: return "Haste %";
			case 36: return "Heroic Charisma";
			case 85: return "Archery Skill Mod %";
			case 86: return "Backstab Skill Mod %";
			case 87: return "Flying Kick Skill Mod %";
			case 88: return "Frenzy Skill Mod %";
			default: return nullptr;
		}
	}

	// One row of #dyninfo worn: what dynamic rolls, other items (and every augment) and set bonuses give
	struct WornTotals {
		int32 dynamic = 0;
		int32 items   = 0;
		int32 sets    = 0;
	};

	// The cap a worn total is held to in game, or 0 when this table has none to show. Base stats and resists
	// are left out: their caps count the character's own stats too, which this table doesn't.
	int32 WornCap(Client *c, EQ::eDynamicAffixID affix)
	{
		Mob *m = c; // the regen and ATK cap helpers are public on Mob
		switch (affix) {
			case EQ::AFFIX_SPELL_CRIT_CHANCE_PCT:
			case EQ::AFFIX_DOT_CRIT_CHANCE_PCT:
			case EQ::AFFIX_HEAL_CRIT_CHANCE_PCT:
			case EQ::AFFIX_HOT_CRIT_CHANCE_PCT:
			case EQ::AFFIX_MELEE_CRIT_CHANCE_PCT:
			case EQ::AFFIX_ARCHERY_CRIT_CHANCE_PCT: return RuleI(Custom, DynamicItemCritChancePctCap);
			case EQ::AFFIX_SPELL_DAMAGE:    return RuleI(Character, ItemSpellDmgCap);
			case EQ::AFFIX_HEAL_AMOUNT:     return RuleI(Character, ItemHealAmtCap);
			case EQ::AFFIX_ATTACK:          return m->CalcItemATKCap();
			case EQ::AFFIX_HP_REGEN:        return static_cast<int32>(m->CalcHPRegenCap());
			case EQ::AFFIX_MANA_REGEN:      return static_cast<int32>(m->CalcManaRegenCap());
			case EQ::AFFIX_ENDURANCE_REGEN: return static_cast<int32>(m->CalcEnduranceRegenCap());
			case EQ::AFFIX_COMBAT_EFFECTS:  return RuleI(Character, ItemCombatEffectsCap);
			case EQ::AFFIX_SPELL_SHIELD:    return RuleI(Character, ItemSpellShieldingCap);
			case EQ::AFFIX_SHIELDING:       return RuleI(Character, ItemShieldingCap);
			case EQ::AFFIX_DAMAGE_SHIELD:   return RuleI(Character, ItemDamageShieldCap);
			case EQ::AFFIX_DOT_SHIELDING:   return RuleI(Character, ItemDoTShieldingCap);
			case EQ::AFFIX_DS_MITIGATION:   return RuleI(Character, ItemDSMitigationCap);
			case EQ::AFFIX_AVOIDANCE:       return RuleI(Character, ItemAvoidanceCap);
			case EQ::AFFIX_ACCURACY:        return RuleI(Character, ItemAccuracyCap);
			case EQ::AFFIX_STUN_RESIST:     return RuleI(Character, ItemStunResistCap);
			case EQ::AFFIX_STRIKE_THROUGH:  return RuleI(Character, ItemStrikethroughCap);
			default: {
				bool is_pct = false;
				TableLabel(affix, is_pct);
				return is_pct ? RuleI(Custom, DynamicItemPctCap) : 0;
			}
		}
	}

	// An item's stats as the affixes they match (the same fields Mob::AddItemBonuses reads)
	void AddItemStats(const EQ::ItemData *item, std::map<EQ::eDynamicAffixID, WornTotals> &totals, std::map<std::string, WornTotals> &other)
	{
		const std::pair<EQ::eDynamicAffixID, int32> stats[] = {
			{ EQ::AFFIX_HP, item->HP },
			{ EQ::AFFIX_MANA, item->Mana },
			{ EQ::AFFIX_ENDURANCE, item->Endur },
			{ EQ::AFFIX_AC, item->AC },
			{ EQ::AFFIX_STR, item->AStr },
			{ EQ::AFFIX_STA, item->ASta },
			{ EQ::AFFIX_DEX, item->ADex },
			{ EQ::AFFIX_AGI, item->AAgi },
			{ EQ::AFFIX_INT, item->AInt },
			{ EQ::AFFIX_WIS, item->AWis },
			{ EQ::AFFIX_CHA, item->ACha },
			{ EQ::AFFIX_HEROIC_STR, item->HeroicStr },
			{ EQ::AFFIX_HEROIC_STA, item->HeroicSta },
			{ EQ::AFFIX_HEROIC_DEX, item->HeroicDex },
			{ EQ::AFFIX_HEROIC_AGI, item->HeroicAgi },
			{ EQ::AFFIX_HEROIC_INT, item->HeroicInt },
			{ EQ::AFFIX_HEROIC_WIS, item->HeroicWis },
			{ EQ::AFFIX_MAGIC_RESIST, item->MR },
			{ EQ::AFFIX_FIRE_RESIST, item->FR },
			{ EQ::AFFIX_COLD_RESIST, item->CR },
			{ EQ::AFFIX_DISEASE_RESIST, item->DR },
			{ EQ::AFFIX_POISON_RESIST, item->PR },
			{ EQ::AFFIX_CORRUPTION_RESIST, item->SVCorruption },
			{ EQ::AFFIX_HEROIC_MAGIC_RESIST, item->HeroicMR },
			{ EQ::AFFIX_HEROIC_FIRE_RESIST, item->HeroicFR },
			{ EQ::AFFIX_HEROIC_COLD_RESIST, item->HeroicCR },
			{ EQ::AFFIX_HEROIC_DISEASE_RESIST, item->HeroicDR },
			{ EQ::AFFIX_HEROIC_POISON_RESIST, item->HeroicPR },
			{ EQ::AFFIX_HEROIC_CORRUPTION_RESIST, item->HeroicSVCorrup },
			{ EQ::AFFIX_SPELL_DAMAGE, item->SpellDmg },
			{ EQ::AFFIX_HEAL_AMOUNT, item->HealAmt },
			{ EQ::AFFIX_ATTACK, item->Attack },
			{ EQ::AFFIX_HP_REGEN, item->Regen },
			{ EQ::AFFIX_MANA_REGEN, item->ManaRegen },
			{ EQ::AFFIX_ENDURANCE_REGEN, item->EnduranceRegen },
			{ EQ::AFFIX_COMBAT_EFFECTS, item->CombatEffects },
			{ EQ::AFFIX_SPELL_SHIELD, item->SpellShield },
			{ EQ::AFFIX_SHIELDING, item->Shielding },
			{ EQ::AFFIX_DAMAGE_SHIELD, item->DamageShield },
			{ EQ::AFFIX_DOT_SHIELDING, item->DotShielding },
			{ EQ::AFFIX_DS_MITIGATION, item->DSMitigation },
			{ EQ::AFFIX_AVOIDANCE, item->Avoidance },
			{ EQ::AFFIX_ACCURACY, item->Accuracy },
			{ EQ::AFFIX_STUN_RESIST, item->StunResist },
			{ EQ::AFFIX_STRIKE_THROUGH, item->StrikeThrough },
		};
		for (const auto &[affix, value] : stats) {
			if (value) {
				totals[affix].items += value;
			}
		}

		if (item->HeroicCha) {
			other["Heroic Charisma"].items += item->HeroicCha;
		}

		// Item haste doesn't add up: the best one counts
		if (item->Haste) {
			auto &haste = other["Haste %"].items;
			haste = std::max<int32>(haste, item->Haste);
		}
	}

	// Totals of everything the character's worn gear gives: dynamic item rolls, other items and every
	// augment (including those in dynamic items), and active armor set bonuses
	void SendDynInfoWorn(Client *c, const std::vector<DynInfoEntry> &entries, int page = 1)
	{
		std::map<EQ::eDynamicAffixID, WornTotals> totals;
		std::map<std::string, WornTotals>         other; // stats with no affix
		int                                       dynamic_worn = 0, items_worn = 0, augs_worn = 0;

		for (const auto &e : entries) {
			if (!e.equipped) {
				continue;
			}
			dynamic_worn++;
			for (const auto &line : e.rolls) {
				totals[line.affix].dynamic += line.value;
			}
		}

		// Worn effects from gear and set bonuses; only the ones that change the totals are listed
		struct WornEffect {
			uint16      spell_id;
			uint8       level;
			std::string source;
		};
		std::vector<WornEffect> worn_effects;
		std::vector<WornEffect> focus_effects;
		auto add_worn_effect = [&](const EQ::ItemData *item) {
			if (item->Worn.Effect > 0 && item->Worn.Type == EQ::item::ItemEffectWorn && IsValidSpell(item->Worn.Effect)) {
				worn_effects.push_back({static_cast<uint16>(item->Worn.Effect), item->Worn.Level, item->Name});
			}
			if (item->Focus.Effect > 0 && item->Focus.Type == EQ::item::ItemEffectFocus && IsValidSpell(item->Focus.Effect)) {
				focus_effects.push_back({static_cast<uint16>(item->Focus.Effect), item->Focus.Level, item->Name});
			}
		};

		// Same slots and checks as Mob::CalcItemBonuses / AddItemBonuses (ammo gives nothing)
		auto mgr    = DynamicItemsManager::Instance();
		auto counts = [&](const EQ::ItemInstance *inst) {
			return inst && inst->IsClassCommon() && inst->GetItem() &&
				inst->IsEquipable(c->GetBaseRace(), c->GetClassesBits()) &&
				c->GetLevel() >= inst->GetItemRequiredLevel(true);
		};
		for (int16 s = EQ::invslot::BONUS_BEGIN; s <= EQ::invslot::BONUS_SKILL_END; ++s) {
			const auto inst = c->GetInv().GetItem(s);
			if (s == EQ::invslot::slotAmmo || !counts(inst)) {
				continue;
			}

			// Dynamic items' own stats are their rolls, counted above
			if (!mgr->IsDynamicItem(inst->GetID())) {
				AddItemStats(inst->GetItem(), totals, other);
				items_worn++;
			}
			add_worn_effect(inst->GetItem());

			for (int a = EQ::invaug::SOCKET_BEGIN; a <= EQ::invaug::SOCKET_END; ++a) {
				const auto aug = inst->GetAugment(a);
				if (aug && aug->GetAugmentType() && aug->GetItem()) {
					AddItemStats(aug->GetItem(), totals, other);
					add_worn_effect(aug->GetItem());
					augs_worn++;
				}
			}
		}

		// Same piece counting and bonus rules as Client::CheckArmorSetBonuses
		const uint32 class_bits = c->GetClassesBits();

		// Sets are labeled by where their pieces are worn, so an armor and a jewelry set with the same name differ
		enum SetKind { SetArmor, SetJewelry, SetWeapons };
		auto set_kind = [](int16 slot) {
			switch (slot) {
				case EQ::invslot::slotPrimary:
				case EQ::invslot::slotSecondary:
				case EQ::invslot::slotRange:
				case EQ::invslot::slotAmmo:
					return SetWeapons;
				case EQ::invslot::slotCharm:
				case EQ::invslot::slotEar1:
				case EQ::invslot::slotEar2:
				case EQ::invslot::slotFace:
				case EQ::invslot::slotNeck:
				case EQ::invslot::slotShoulders:
				case EQ::invslot::slotBack:
				case EQ::invslot::slotWaist:
				case EQ::invslot::slotFinger1:
				case EQ::invslot::slotFinger2:
					return SetJewelry;
				default:
					return SetArmor;
			}
		};
		static const char *set_kind_names[] = {"Armor", "Jewelry", "Weapons"};

		std::vector<std::tuple<SetKind, std::string, std::string>> set_entries; // kind, name, lines
		for (const auto &[set_id, pieces] : c->CountEquippedArmorSetPieces()) {
			const auto bonuses = g_armor_set_bonuses_map.find(set_id);
			if (bonuses == g_armor_set_bonuses_map.end()) {
				continue;
			}

			// Name the set after a worn piece ("Bladesinger's Regalia Helm" -> "Bladesinger's Regalia")
			std::string name = fmt::format("Set {}", set_id);
			SetKind     kind = SetArmor;
			for (int16 s = EQ::invslot::EQUIPMENT_BEGIN; s <= EQ::invslot::EQUIPMENT_END; ++s) {
				const auto inst = c->GetInv().GetItem(s);
				const auto it   = inst ? g_armor_set_item_map.find(inst->GetID()) : g_armor_set_item_map.end();
				const auto base = it != g_armor_set_item_map.end() && it->second.set_id == set_id
					? database.GetItem(inst->GetID()) // the item row's name, without a dynamic roll suffix
					: nullptr;
				if (base) {
					name = base->Name;
					if (const auto space = name.rfind(' '); space != std::string::npos) {
						name.resize(space);
					}
					kind = set_kind(s);
					break;
				}
			}

			std::vector<uint32>      thresholds, active;
			std::vector<std::string> spell_bonuses; // active worn effects and procs, by name
			for (const auto &b : bonuses->second) {
				if (std::find(thresholds.begin(), thresholds.end(), b.min_pieces) == thresholds.end()) {
					thresholds.push_back(b.min_pieces);
				}
				if (pieces < b.min_pieces || (b.class_mask && !(b.class_mask & class_bits))) {
					continue;
				}
				if (std::find(active.begin(), active.end(), b.min_pieces) == active.end()) {
					active.push_back(b.min_pieces);
				}

				if (b.spell_id && b.spell_type == ArmorSetSpellWorn) {
					worn_effects.push_back({b.spell_id, c->GetLevel(), fmt::format("{} {} set", name, set_kind_names[kind])});
					continue;
				}
				if (b.spell_id) {
					spell_bonuses.push_back(fmt::format(
						"{} ({})",
						spells[b.spell_id].name,
						b.spell_type == ArmorSetSpellWorn ? "worn" : b.spell_type == ArmorSetSpellMeleeProc ? "melee proc" : "spell proc"
					));
					continue;
				}

				const auto affix = b.affix != EQ::AFFIX_NONE ? b.affix : SetStatAffix(b.stat_id);
				if (affix != EQ::AFFIX_NONE) {
					totals[affix].sets += b.stat_value;
				} else if (const auto label = SetStatLabel(b.stat_id)) {
					other[label].sets += b.stat_value;
				}
			}
			std::sort(thresholds.begin(), thresholds.end());
			std::sort(active.begin(), active.end());

			auto join = [](const std::vector<uint32> &v) {
				std::vector<std::string> s;
				for (const auto n : v) {
					s.push_back(std::to_string(n));
				}
				return s.empty() ? std::string("none") : Strings::Join(s, "/");
			};

			std::string lines = fmt::format(
				"{} {} - {} worn, bonuses at {}, active: {}",
				DialogueWindow::ColorMessage("white", name),
				DialogueWindow::ColorMessage("dark_grey", fmt::format("({})", set_kind_names[kind])),
				pieces,
				join(thresholds),
				active.empty() ? DialogueWindow::ColorMessage("red", "none") : DialogueWindow::ColorMessage("green", join(active))
			) + DialogueWindow::Break();
			for (const auto &spell_bonus : spell_bonuses) {
				lines += "&nbsp;&nbsp;" + DialogueWindow::ColorMessage("light_blue_1", spell_bonus) + DialogueWindow::Break();
			}
			set_entries.emplace_back(kind, name, std::move(lines));
		}

		// Armor, then jewelry, then weapons; by name within each
		std::sort(set_entries.begin(), set_entries.end());
		std::string set_lines;
		for (const auto &e : set_entries) {
			set_lines += std::get<2>(e);
		}

		// Worn effects of one family don't stack (highest wins, Spells:AdditiveBonusWornType), so drop each
		// effect whose removal leaves the bonuses unchanged; of two equal effects the later one is kept
		auto worn_bonuses = [&](const std::vector<size_t> &keep, size_t skip) {
			StatBonuses b;
			memset(&b, 0, sizeof(StatBonuses));
			for (const auto i : keep) {
				if (i != skip) {
					c->ApplySpellsBonuses(worn_effects[i].spell_id, worn_effects[i].level, &b, 0, EQ::item::ItemEffectWorn);
				}
			}
			return b;
		};
		std::vector<size_t> working(worn_effects.size());
		std::iota(working.begin(), working.end(), 0);
		const auto all_worn = worn_bonuses(working, SIZE_MAX);
		for (size_t i = 0; i < worn_effects.size(); ++i) {
			const auto without = worn_bonuses(working, i);
			if (!memcmp(&without, &all_worn, sizeof(StatBonuses))) {
				working.erase(std::find(working.begin(), working.end(), i));
			}
		}
		if (!working.empty()) {
			set_lines += DialogueWindow::ColorMessage("white", "Worn effects") + DialogueWindow::Break();
			for (const auto i : working) {
				set_lines += "&nbsp;&nbsp;" + DialogueWindow::ColorMessage(
					"light_blue_1",
					fmt::format("{} ({})", spells[worn_effects[i].spell_id].name, worn_effects[i].source)
				) + DialogueWindow::Break();
			}
		}

		// Item focuses don't stack either: each cast uses the best item focus of each focus type. Copies of one
		// focus are listed once, and a focus is dropped when another with the same focus types and the same limits
		// is at least as strong, since it can never be the one picked.
		std::vector<std::pair<uint16, std::vector<std::string>>> focuses; // spell id, sources
		for (const auto &f : focus_effects) {
			auto it = std::find_if(focuses.begin(), focuses.end(), [&](const auto &e) { return e.first == f.spell_id; });
			if (it == focuses.end()) {
				focuses.push_back({f.spell_id, {f.source}});
			} else if (std::find(it->second.begin(), it->second.end(), f.source) == it->second.end()) {
				it->second.push_back(f.source);
			}
		}
		auto focus_shape = [&](uint16 spell_id, bool focus_slots) {
			std::vector<std::tuple<int, int, int, int>> shape; // effect, base, limit, max
			const auto &sp = spells[spell_id];
			for (int i = 0; i < EFFECT_COUNT; ++i) {
				if (!IsBlankSpellEffect(spell_id, i) && (c->IsFocusEffect(spell_id, i) != 0) == focus_slots) {
					shape.emplace_back(sp.effect_id[i], sp.base_value[i], sp.limit_value[i], sp.max_value[i]);
				}
			}
			return shape;
		};
		auto outclassed_by = [&](uint16 weak, uint16 strong) {
			if (focus_shape(weak, false) != focus_shape(strong, false)) {
				return false; // different limits: they can apply to different spells
			}
			const auto w = focus_shape(weak, true);
			const auto s = focus_shape(strong, true);
			if (w.size() != s.size()) {
				return false;
			}
			for (size_t i = 0; i < w.size(); ++i) {
				if (std::get<0>(w[i]) != std::get<0>(s[i]) ||
					std::get<1>(w[i]) > std::get<1>(s[i]) || std::get<2>(w[i]) > std::get<2>(s[i])) {
					return false;
				}
			}
			return true;
		};
		std::vector<size_t> working_focuses;
		for (size_t i = 0; i < focuses.size(); ++i) {
			bool shadowed = false;
			for (size_t j = 0; j < focuses.size() && !shadowed; ++j) {
				// Of two equal focuses the first one listed is kept
				shadowed = j != i && outclassed_by(focuses[i].first, focuses[j].first) &&
					(j < i || !outclassed_by(focuses[j].first, focuses[i].first));
			}
			if (!shadowed) {
				working_focuses.push_back(i);
			}
		}
		if (!working_focuses.empty()) {
			set_lines += DialogueWindow::ColorMessage("white", "Focus effects") + DialogueWindow::Break();
			for (const auto i : working_focuses) {
				set_lines += "&nbsp;&nbsp;" + DialogueWindow::ColorMessage(
					"light_blue_1",
					fmt::format("{} ({})", spells[focuses[i].first].name, Strings::Join(focuses[i].second, ", "))
				) + DialogueWindow::Break();
			}
		}

		if (totals.empty() && other.empty()) {
			c->Message(Chat::White, "Your worn gear doesn't give any stats.");
			return;
		}

		auto cell = [](int32 value, const char *unit) {
			return DialogueWindow::TableCell(value ? fmt::format("{}{}", value, unit) : "-");
		};
		// A total over its cap shows the cap beside it: only the capped amount counts in game
		auto row = [&](const std::string &name, const WornTotals &v, bool is_pct, int32 cap = 0) {
			const auto  unit  = is_pct ? "%" : "";
			const int32 total = v.dynamic + v.items + v.sets;
			std::string shown = DialogueWindow::ColorMessage("gold", fmt::format("{}{}", total, unit));
			if (cap > 0 && total > cap) {
				shown += " " + DialogueWindow::ColorMessage("red", fmt::format("(cap {}{})", cap, unit));
			}
			return DialogueWindow::TableRow(
				DialogueWindow::TableCell(name) + cell(v.dynamic, unit) + cell(v.items, unit) + cell(v.sets, unit) +
				DialogueWindow::TableCell(shown)
			);
		};

		std::vector<std::string> rows;
		for (const auto &[affix, v] : totals) {
			bool       is_pct = false;
			const auto name   = TableLabel(affix, is_pct);
			rows.push_back(row(name, v, is_pct, WornCap(c, affix)));

			// Heroic Charisma has no affix of its own: list it with the other heroics
			if (affix == EQ::AFFIX_HEROIC_WIS && other.contains("Heroic Charisma")) {
				rows.push_back(row("Heroic Charisma", other["Heroic Charisma"], false));
			}
		}
		const bool heroic_cha_listed = totals.contains(EQ::AFFIX_HEROIC_WIS);
		for (const auto &[label, v] : other) {
			if (label == "Heroic Charisma" && heroic_cha_listed) {
				continue;
			}
			std::string name   = label;
			const bool  is_pct = name.ends_with(" %");
			if (is_pct) {
				name.resize(name.size() - 2);
			}
			rows.push_back(row(name, v, is_pct));
		}

		const std::string top = DialogueWindow::ColorMessage(
			"gold",
			fmt::format(
				"{} dynamic, {} other item{}, {} augment{} worn",
				dynamic_worn,
				items_worn,
				items_worn == 1 ? "" : "s",
				augs_worn,
				augs_worn == 1 ? "" : "s"
			)
		) + DialogueWindow::Break() + set_lines;
		const std::string header = DialogueWindow::TableCell("Stat") + DialogueWindow::TableCell("Dynamic") +
			DialogueWindow::TableCell("Items") + DialogueWindow::TableCell("Sets") + DialogueWindow::TableCell("Total");
		const std::string bottom = "Items = other worn items and all augments. Gear only, not buffs or AAs. A red (cap) means only that much counts; base stat and resist caps also apply in game.";

		SendPagedTable(c, "Worn Totals", top, header, rows, bottom, page, "#dyninfo worn");
	}
}

void command_dyninfo(Client *c, const Seperator *sep)
{
	const auto entries = GatherDynamicItems(c);

	// Before the empty check: set pieces that aren't dynamic items still have bonuses to total
	if (sep->argnum >= 1 && !strcasecmp(sep->arg[1], "worn")) {
		SendDynInfoWorn(c, entries, sep->IsNumber(2) ? Strings::ToInt(sep->arg[2]) : 1);
		return;
	}

	if (entries.empty()) {
		c->Message(Chat::White, "You don't have any dynamic items.");
		return;
	}

	if (sep->argnum == 0) {
		SendDynInfoOverview(c, entries);
		return;
	}

	if (sep->IsNumber(1)) {
		SendDynInfoItem(c, entries, Strings::ToInt(sep->arg[1]), sep->IsNumber(2) ? Strings::ToInt(sep->arg[2]) : 1);
		return;
	}

	if (!strcasecmp(sep->arg[1], "compare")) {
		if (!sep->IsNumber(2) || !sep->IsNumber(3)) {
			c->Message(Chat::White, "Usage: #dyninfo compare [#] [#] [page] - numbers are shown by #dyninfo");
			return;
		}

		SendDynInfoCompare(
			c,
			entries,
			Strings::ToInt(sep->arg[2]),
			Strings::ToInt(sep->arg[3]),
			sep->IsNumber(4) ? Strings::ToInt(sep->arg[4]) : 1
		);
		return;
	}

	// Allow two-word slot names ("power source")
	std::string slot_name = sep->arg[1];
	if (sep->argnum >= 2) {
		slot_name += sep->arg[2];
	}

	int group = DynamicItemsManager::SlotGroupFromName(sep->arg[1]);
	if (group < 0) {
		group = DynamicItemsManager::SlotGroupFromName(slot_name);
	}

	if (group < 0) {
		c->Message(Chat::White, "Usage: #dyninfo - overview | #dyninfo [#] [page] - compare with worn | #dyninfo [slot] - e.g. feet | #dyninfo compare [#] [#] [page] | #dyninfo worn [page] - totals of everything worn");
		return;
	}

	SendDynInfoSlot(c, entries, group);
}
