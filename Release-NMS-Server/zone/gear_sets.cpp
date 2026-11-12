#include "gear_sets.h"

#include "../common/eq_constants.h"
#include "../common/eq_packet_structs.h"
#include "../common/eqemu_logsys.h"
#include "../common/item_instance.h"
#include "../common/strings.h"
#include "client.h"
#include "zonedb.h"

#include <fmt/format.h>
#include <map>
#include <set>

extern ZoneDatabase database;

namespace {
	bool ValidSet(uint8 set_number)
	{
		return set_number < EQ::profile::BANDOLIERS_SIZE;
	}

	bool IsWeaponSlot(int16 slot_id)
	{
		return slot_id == EQ::invslot::slotPrimary || slot_id == EQ::invslot::slotSecondary ||
			slot_id == EQ::invslot::slotRange || slot_id == EQ::invslot::slotAmmo;
	}

	// Where a set piece may be found: worn, the general slots and the bags in them (not the cursor or bank)
	std::vector<int16> SearchSlots(Client *c)
	{
		std::vector<int16> slots;
		for (int16 s = EQ::invslot::EQUIPMENT_BEGIN; s <= EQ::invslot::EQUIPMENT_END; ++s) {
			slots.push_back(s);
		}
		auto &inv = c->GetInv();
		for (int16 s = EQ::invslot::GENERAL_BEGIN; s <= EQ::invslot::GENERAL_END; ++s) {
			slots.push_back(s);
			const auto *bag = inv.GetItem(s);
			if (bag && bag->IsClassBag()) {
				const int16 base = EQ::InventoryProfile::CalcSlotId(s, EQ::invbag::SLOT_BEGIN);
				for (uint8 b = EQ::invbag::SLOT_BEGIN; b < bag->GetItem()->BagSlots; ++b) {
					slots.push_back(base + b);
				}
			}
		}
		return slots;
	}

	// 3 = this exact item, 2 = same item and roll (dynamic items), 1 = same item, 0 = no
	int MatchScore(const EQ::ItemInstance *inst, const GearSetEntry &want)
	{
		if (!inst || !inst->GetItem() || inst->GetID() != want.item_id) {
			return 0;
		}
		if (want.guid && static_cast<uint64>(inst->GetSerialNumber()) == want.guid) {
			return 3;
		}
		return inst->GetCustomDataString() == want.custom_data ? 2 : 1;
	}

	void SendClearSlot(Client *c, int16 slot_id)
	{
		auto outapp = new EQApplicationPacket(OP_MoveItem, sizeof(MoveItem_Struct));
		auto *mi = reinterpret_cast<MoveItem_Struct *>(outapp->pBuffer);
		mi->from_slot       = slot_id;
		mi->to_slot         = 0xFFFFFFFF;
		mi->number_in_stack = 0xFFFFFFFF;
		c->QueuePacket(outapp);
		safe_delete(outapp);
	}
}

std::vector<GearSetEntry> GearSetLoad(uint32 character_id, uint8 set_number)
{
	std::vector<GearSetEntry> out;
	auto results = database.QueryDatabase(fmt::format(
		"SELECT slot_id, item_id, guid, custom_data FROM character_gear_sets WHERE character_id = {} AND set_number = {} "
		"ORDER BY slot_id",
		character_id, set_number
	));
	if (!results.Success()) {
		return out;
	}
	for (auto row : results) {
		GearSetEntry e;
		e.slot_id     = static_cast<int16>(Strings::ToInt(row[0]));
		e.item_id     = Strings::ToUnsignedInt(row[1]);
		e.guid        = Strings::ToUnsignedBigInt(row[2]);
		e.custom_data = row[3] ? row[3] : "";
		out.push_back(e);
	}
	return out;
}

void GearSetSave(Client *c, uint8 set_number)
{
	if (!c || !ValidSet(set_number)) {
		return;
	}
	GearSetDelete(c, set_number);

	std::vector<std::string> values;
	auto &inv = c->GetInv();
	for (int16 s = EQ::invslot::EQUIPMENT_BEGIN; s <= EQ::invslot::EQUIPMENT_END; ++s) {
		const auto *inst = inv.GetItem(s);
		if (!inst || !inst->GetItem()) {
			continue;
		}
		values.push_back(fmt::format(
			"({}, {}, {}, {}, {}, '{}')",
			c->CharacterID(), set_number, s, inst->GetID(), static_cast<uint64>(inst->GetSerialNumber()),
			Strings::Escape(inst->GetCustomDataString())
		));
	}
	if (values.empty()) {
		return;
	}
	database.QueryDatabase(fmt::format(
		"INSERT INTO character_gear_sets (character_id, set_number, slot_id, item_id, guid, custom_data) VALUES {}",
		Strings::Implode(", ", values)
	));
}

void GearSetDelete(Client *c, uint8 set_number)
{
	if (!c || !ValidSet(set_number)) {
		return;
	}
	database.QueryDatabase(fmt::format(
		"DELETE FROM character_gear_sets WHERE character_id = {} AND set_number = {}", c->CharacterID(), set_number
	));
}

bool GearSetApply(Client *c, uint8 set_number, bool include_weapons)
{
	if (!c || !ValidSet(set_number)) {
		return false;
	}

	std::vector<GearSetEntry> wants;
	for (const auto &e : GearSetLoad(c->CharacterID(), set_number)) {
		if (e.slot_id < EQ::invslot::EQUIPMENT_BEGIN || e.slot_id > EQ::invslot::EQUIPMENT_END) {
			continue;
		}
		if (!include_weapons && IsWeaponSlot(e.slot_id)) {
			continue;
		}
		wants.push_back(e);
	}
	if (wants.empty()) {
		return false;
	}

	auto &inv = c->GetInv();
	const auto search = SearchSlots(c);

	// 1. Find where each piece is. A piece already in its slot stays; the best match wins, and no item
	//    is used twice (two identical rings).
	std::map<int16, int16> source_of; // target slot -> slot the piece is in now
	std::set<int16> taken;
	std::vector<uint32> missing;
	for (const auto &w : wants) {
		if (MatchScore(inv.GetItem(w.slot_id), w) >= 2) {
			source_of[w.slot_id] = w.slot_id;
			taken.insert(w.slot_id);
		}
	}
	for (const auto &w : wants) {
		if (source_of.count(w.slot_id)) {
			continue;
		}
		int16 best = INVALID_INDEX;
		int best_score = 0;
		for (int16 s : search) {
			if (taken.count(s)) {
				continue;
			}
			const int score = MatchScore(inv.GetItem(s), w);
			if (score > best_score) {
				best = s;
				best_score = score;
			}
		}
		if (best == INVALID_INDEX) {
			missing.push_back(w.item_id);
			continue;
		}
		source_of[w.slot_id] = best;
		taken.insert(best);
	}

	// 2. Do the whole swap in memory. Anything that doesn't fit puts every slot back as it was, so the
	//    client and the database only ever see a finished swap.
	std::map<int16, EQ::ItemInstance *> original; // every touched slot as it was (nullptr = empty)
	auto remember = [&](int16 s) {
		if (!original.count(s)) {
			const auto *cur = inv.GetItem(s);
			original[s] = cur ? cur->Clone() : nullptr;
		}
	};
	std::vector<std::pair<int16, int16>> moves; // target, source
	for (const auto &kv : source_of) {
		if (kv.first != kv.second) {
			moves.emplace_back(kv.first, kv.second);
			remember(kv.first);
			remember(kv.second);
		}
	}

	auto free_original = [&]() {
		for (auto &kv : original) {
			safe_delete(kv.second);
		}
	};

	auto report_missing = [&]() {
		if (missing.empty()) {
			return;
		}
		std::vector<std::string> names;
		for (uint32 id : missing) {
			const auto *item = database.GetItem(id);
			names.push_back(item ? item->Name : std::to_string(id));
		}
		c->Message(Chat::Yellow, fmt::format("[NMS] Not on you: {}.", Strings::Implode(", ", names)).c_str());
	};

	if (moves.empty()) {
		free_original();
		report_missing();
		return false;
	}

	std::map<int16, EQ::ItemInstance *> pieces; // source slot -> piece
	for (const auto &m : moves) {
		pieces[m.second] = inv.PopItem(m.second);
	}
	std::vector<EQ::ItemInstance *> displaced;
	for (const auto &m : moves) {
		if (auto *cur = inv.PopItem(m.first)) {
			displaced.push_back(cur);
		}
	}
	for (const auto &m : moves) {
		inv.PutItem(m.first, *pieces[m.second]);
	}

	bool fits = true;
	for (auto *d : displaced) {
		const auto *data = d->GetItem();
		const int16 slot = inv.FindFreeSlot(d->IsClassBag(), false, data->Size, data->ItemType == EQ::item::ItemTypeArrow);
		if (slot == INVALID_INDEX) {
			fits = false;
			break;
		}
		remember(slot);
		if (inv.PutItem(slot, *d) == EQ::invslot::SLOT_INVALID) {
			fits = false;
			break;
		}
	}

	for (auto &kv : pieces) {
		safe_delete(kv.second);
	}
	for (auto *d : displaced) {
		safe_delete(d);
	}

	if (!fits) {
		for (auto &kv : original) {
			inv.DeleteItem(kv.first);
			if (kv.second) {
				inv.PutItem(kv.first, *kv.second);
			}
		}
		free_original();
		c->Message(Chat::Red, "[NMS] Gear set: not enough room in your bags for the gear coming off. Nothing was swapped.");
		return false;
	}

	// 3. Tell the client and save: clear every touched slot it had something in, then send what is there now
	for (auto &kv : original) {
		if (kv.second) {
			SendClearSlot(c, kv.first);
		}
	}
	for (auto &kv : original) {
		const auto *now = inv.GetItem(kv.first);
		if (now) {
			c->SendItemPacket(kv.first, now, ItemPacketTrade);
		}
		database.SaveInventory(c->CharacterID(), now, kv.first);
		if (kv.first >= EQ::invslot::EQUIPMENT_BEGIN && kv.first <= EQ::invslot::EQUIPMENT_END) {
			const uint8 material = EQ::InventoryProfile::CalcMaterialFromSlot(kv.first);
			if (material != EQ::textures::materialInvalid) {
				c->SendWearChange(material);
			}
		}
	}
	free_original();
	c->CalcBonuses();

	c->Message(
		Chat::Yellow,
		fmt::format("[NMS] Gear set {}: {} piece{} swapped.", c->GetBandolierName(set_number), moves.size(), moves.size() == 1 ? "" : "s").c_str()
	);
	report_missing();
	return true;
}
