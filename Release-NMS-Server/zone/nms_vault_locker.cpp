/*
 * NMS-LOCAL: the Dimensional Pocket Vault's Proc Locker (vault page 9).
 *
 * Pri (slot 81) and Rng (slot 83) contribute their combat proc only. Sec
 * (slot 82) contributes shield stats and shield-equipped status only when the
 * real secondary slot is empty. Pri and Rng stay inactive when the equipped
 * weapon already contains a combat-proc augment.
 */

#include "client.h"
#include "zone.h"
#include "../common/classes.h"
#include "../common/races.h"

static const uint8  NMS_LOCKER_PAGE     = 9;
static const uint16 NMS_LOCKER_SLOT_PRI = 81;
static const uint16 NMS_LOCKER_SLOT_SEC = 82;
static const uint16 NMS_LOCKER_SLOT_RNG = 83;

void Client::ClearNMSVaultLocker()
{
	for (int i = 0; i < NMS_LOCKER_SLOTS; ++i) {
		safe_delete(m_nms_locker[i]);
	}
}

bool Client::NMSVaultLockerCanUse(const EQ::ItemData *item)
{
	if (!item) {
		return false;
	}

	if (item->ReqLevel && GetLevel() < item->ReqLevel) {
		return false;
	}

	if (item->Classes && !(item->Classes & GetClassesBits())) {
		return false;
	}

	if (item->Races && !(item->Races & GetPlayerRaceBit(GetBaseRace()))) {
		return false;
	}

	return true;
}

void Client::LoadNMSVaultLocker()
{
	ClearNMSVaultLocker();

	const auto query = fmt::format(
		"SELECT slot, item_id, charges, augment_one, augment_two, augment_three, "
		"augment_four, augment_five, augment_six "
		"FROM nms_vault WHERE character_id = {} AND page = {}",
		CharacterID(), NMS_LOCKER_PAGE
	);

	auto results = database.QueryDatabase(query);
	if (!results.Success()) {
		return;
	}

	for (auto row = results.begin(); row != results.end(); ++row) {
		const uint16 slot    = static_cast<uint16>(std::stoul(row[0]));
		const uint32 item_id = static_cast<uint32>(std::stoul(row[1]));
		const int16  charges = static_cast<int16>(std::stoi(row[2]));

		int index = -1;
		switch (slot) {
			case NMS_LOCKER_SLOT_PRI: index = 0; break;
			case NMS_LOCKER_SLOT_SEC: index = 1; break;
			case NMS_LOCKER_SLOT_RNG: index = 2; break;
			default: continue;
		}

		m_nms_locker[index] = database.CreateItem(
			item_id,
			charges ? charges : 1,
			static_cast<uint32>(std::stoul(row[3])),
			static_cast<uint32>(std::stoul(row[4])),
			static_cast<uint32>(std::stoul(row[5])),
			static_cast<uint32>(std::stoul(row[6])),
			static_cast<uint32>(std::stoul(row[7])),
			static_cast<uint32>(std::stoul(row[8]))
		);
	}
}

void Client::CalcNMSVaultLockerBonuses(StatBonuses *bonuses)
{
	if (m_nms_locker[1] && !GetInv().GetItem(EQ::invslot::slotSecondary)) {
		const auto *item = m_nms_locker[1]->GetItem();
		if (item && item->ItemType == EQ::item::ItemTypeShield && NMSVaultLockerCanUse(item)) {
			AddItemBonuses(m_nms_locker[1], bonuses);
			SetShieldEquipped(true);
		}
	}
}

bool Client::NMSVaultLockerHasAugProc(int16 slot)
{
	const auto *equipped = GetInv().GetItem(slot);
	if (!equipped) {
		return false;
	}

	for (uint8 i = EQ::invaug::SOCKET_BEGIN; i <= EQ::invaug::SOCKET_END; ++i) {
		const auto *augment = equipped->GetAugment(i);
		if (!augment) {
			continue;
		}

		const auto *item = augment->GetItem();
		if (item &&
			item->Proc.Type == EQ::item::ItemEffectCombatProc &&
			IsValidSpell(item->Proc.Effect)) {
			return true;
		}
	}

	return false;
}

void Client::TryNMSVaultLockerProc(Mob *on, uint16 hand, float proc_chance, int our_level)
{
	if (!on) {
		return;
	}

	int   index = -1;
	int16 equip_slot = 0;
	if (hand == EQ::invslot::slotPrimary) {
		index = 0;
		equip_slot = EQ::invslot::slotPrimary;
	}
	else if (hand == EQ::invslot::slotRange) {
		index = 2;
		equip_slot = EQ::invslot::slotRange;
	}
	else {
		return;   // Sec grants stats, not procs
	}

	if (!m_nms_locker[index]) {
		return;
	}

	// "does not stack with augment procs"
	if (NMSVaultLockerHasAugProc(equip_slot)) {
		return;
	}

	const auto *item = m_nms_locker[index]->GetItem();
	if (!item || !NMSVaultLockerCanUse(item)) {
		return;
	}

	if (item->Proc.Type != EQ::item::ItemEffectCombatProc || !IsValidSpell(item->Proc.Effect)) {
		return;
	}

	const float apc = proc_chance * (100.0f + static_cast<float>(item->ProcRate)) / 100.0f;
	if (!zone->random.Roll(apc)) {
		return;
	}

	if (item->Proc.Level2 > our_level) {
		MessageString(Chat::Red, PROC_TOOLOW);
		return;
	}

	ExecWeaponProc(m_nms_locker[index], item->Proc.Effect, on);
}

void Client::ReloadNMSVaultLocker()
{
	LoadNMSVaultLocker();
	CalcBonuses();
}
