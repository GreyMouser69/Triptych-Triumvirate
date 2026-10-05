/*
 * NMS-LOCAL: loot-offer protocol for the Triune client DLL.
 *
 * The client DLL's /nmsloot window populates its "Pending" tab exclusively from
 * opcode 0x140A. Nothing else reaches that tab -- summoning an item straight to
 * inventory bypasses the corpse, the doLoot hook and this packet entirely.
 *
 * WIRE FORMAT
 * -----------
 * The layout below is ported from BFE (Brother's Foothold Evolved), where it was
 * recovered by disassembling the DLL (parser at va 0x1000f250). See
 * EQagent/TRIUNE_DLL_PROTOCOL.md there for the full derivation. The client
 * rejects anything shorter than 0x56 + count * 0x96 with:
 *     "[NMS] Discarded a malformed loot offer packet."
 *
 * Fields marked unknown are sent as zero. They are not read by the parse path
 * that was followed, but that path was not exhaustive -- if the Pending tab
 * renders items with wrong icons or blank rows, they are the first suspects.
 *
 * Triptych adaptation (the one that matters)
 * ------------------------------------------
 * Every place BFE hands an item back to the player calls
 * SummonItemIntoInventory(..., allow_upgrade=false). That parameter DOES NOT
 * EXIST here. Triptych's SummonItemIntoInventory (inventory.cpp:5607) creates
 * an instance and then calls SummonApocItem (inventory.cpp:269), which rewrites
 * item_id -- +1,000,000, then a per-item roll -- whenever Custom:DoItemUpgrades
 * is enabled. An item returned because the player never decided on it would come
 * back UPGRADED, which is both wrong and a dupe vector for anything whose
 * upgraded tier is worth more.
 *
 * So all returns go through ReturnNMSLootItemExact()/PlaceNMSLootItemExact(),
 * which build the instance with database.CreateItem() and hand it to
 * AutoPutLootInInventory()/PutLootInInventory(). Neither of those touches
 * SummonApocItem, so the item id is preserved exactly.
 *
 * OWNERSHIP: PutLootInInventory() takes a const EQ::ItemInstance& and clones it
 * into the inventory profile (inventory_profile.cpp:223 -> _PutItem(...,
 * inst.Clone())). The caller keeps ownership of the instance it built, so both
 * helpers below delete theirs. Nothing here is double-deleted: on the success
 * path the inventory holds a clone, on the failure path we delete the original.
 *
 * The cursor is a FIFO QUEUE (ItemInstQueue m_cursor), not a single slot, and both
 * PutLootInInventory and PushItemOnCursor append to it via PushCursor(), which
 * also clones. A returned item therefore queues BEHIND whatever is already on the
 * cursor; it never replaces or deletes it.
 */

#include "client.h"
#include "quest_parser_collection.h"
#include "zone.h"
#include "../common/eqemu_logsys.h"
#include "../common/eq_packet_structs.h"
#include "../common/opcodemgr.h"
#include "../common/rulesys.h"
#include "../common/strings.h"
#include <cstring>
#include <sstream>

// Decision codes, ported from BFE. They are the index of the action list in the
// client's LootList.ini:  None Keep Sell Tribute Bank Vault Destroy
//   0 = acknowledgement, sent ~1s after every offer (entry_id 0, item_ref 0xFFFE)
//   1 = Keep
//   2 = Sell
//   3 = Tribute
//   4 = Bank
//   5 = Vault
//   6 = Destroy
//   9 = Pass -- the client's Send tab, AutoPass and ignore-list returns. The
//       reply's name field holds the TARGET player; see Handle_OP_NMSLootReply.
// "Loot All (List)" and "(No List)" both send KEEP; "Sell All" sends SELL. The
// (List)/(No List) distinction is client-side only -- whether a rule is written.
static const uint8 NMSLOOT_ACTION_ACK     = 0;
static const uint8 NMSLOOT_ACTION_KEEP    = 1;
static const uint8 NMSLOOT_ACTION_SELL    = 2;
static const uint8 NMSLOOT_ACTION_TRIBUTE = 3;
static const uint8 NMSLOOT_ACTION_BANK    = 4;
static const uint8 NMSLOOT_ACTION_VAULT   = 5;
static const uint8 NMSLOOT_ACTION_DESTROY = 6;
static const uint8 NMSLOOT_ACTION_PASS    = 9;

// The client clamps these two itself, but never send more than it will read.
static const size_t NMSLOOT_MAX_OFFERS  = 64;
static const size_t NMSLOOT_MAX_ROSTER  = 20;
static const size_t NMSLOOT_NAME_LEN    = 64;

#pragma pack(1)

// 150 bytes (0x96) per entry -- stride confirmed from BFE's disassembly (imul).
struct NMSLootEntry_Struct {
	uint32_t id;                // 0x00  echoed back in the 0x140B reply; dedup key
	uint8_t  unknown_04[8];     // 0x04  unidentified
	uint32_t item_ref;          // 0x0C  echoed back in the reply; likely item id
	uint8_t  unknown_10[6];     // 0x10  unidentified
	char     item_name[64];     // 0x16  NUL-terminated
	char     from_player[64];   // 0x56  NUL-terminated; entry SKIPPED by client if empty
};

// 86-byte (0x56) header, then entries[count].
struct NMSLootOffer_Struct {
	uint16_t field_00;          // 0x00  echoed back in the reply
	char     header_name[64];   // 0x02
	uint32_t unknown_42;        // 0x42
	uint32_t unknown_46;        // 0x46
	uint32_t unknown_4A;        // 0x4A
	uint32_t unknown_4E;        // 0x4E
	uint32_t count;             // 0x52  client clamps this to 64
	NMSLootEntry_Struct entries[0];
};

// Opcode 0x140D -- the Box Looting Control roster. BFE's handler at 0x1000ff30
// demands >= 0x504 (1284) bytes and clamps the count to 0x14 (20). Names are
// strncpy'd 63 + explicit NUL into a global array at 0x1034dbc0.
struct NMSLooterList_Struct {
	uint32_t count;              // 0x00  clamped client-side to 20
	char     names[20][64];      // 0x04
};

// 75 bytes of payload; the DLL writes the opcode into its own buffer, so the
// packet body the server receives is 75, not 77.
struct NMSLootReply_Struct {
	uint16_t header_field_00;
	uint32_t entry_field_0C;
	uint32_t entry_id;
	uint8_t  action;             // 0 = ack, 1 = Keep, 2 = other decision
	char     from_player[64];
};

#pragma pack()

static_assert(sizeof(NMSLootEntry_Struct) == 0x96, "NMSLootEntry must be 150 bytes");
static_assert(sizeof(NMSLootOffer_Struct) == 0x56, "NMSLootOffer header must be 86 bytes");
static_assert(sizeof(NMSLooterList_Struct) == 0x504, "NMSLooterList must be 1284 bytes");
static_assert(sizeof(NMSLootReply_Struct) == 75, "NMSLootReply body must be 75 bytes");

// Both structs are read back by casting app->pBuffer, which is only 1-byte
// aligned. Their naturally aligned members would fault on strict-alignment
// targets, so every field is pulled out with memcpy rather than dereferenced.
static_assert(offsetof(NMSLootEntry_Struct, id) == 0x00, "NMSLootEntry id offset");
static_assert(offsetof(NMSLootEntry_Struct, item_ref) == 0x0C, "NMSLootEntry item_ref offset");
static_assert(offsetof(NMSLootEntry_Struct, item_name) == 0x16, "NMSLootEntry item_name offset");
static_assert(offsetof(NMSLootEntry_Struct, from_player) == 0x56, "NMSLootEntry from_player offset");
static_assert(offsetof(NMSLootOffer_Struct, count) == 0x52, "NMSLootOffer count offset");

/*
 * Format a copper amount the way a player expects to read it. AddMoneyToPP()
 * already splits the value into platinum/gold/silver/copper internally, so only
 * the message was ever wrong -- "1250 copper" instead of "1p 2g 5s".
 */
static std::string NMSFormatCoin(uint64 copper)
{
	const uint64 pp = copper / 1000;
	const uint64 gp = (copper % 1000) / 100;
	const uint64 sp = (copper % 100) / 10;
	const uint64 cp = copper % 10;

	std::string out;
	if (pp) out += fmt::format("{}p ", pp);
	if (gp) out += fmt::format("{}g ", gp);
	if (sp) out += fmt::format("{}s ", sp);
	if (cp || out.empty()) out += fmt::format("{}c ", cp);
	out.pop_back();
	return out;
}

/*
 * What the SELL action pays for one offer row (all its copies). Paying full
 * vendor value would be far too generous for an automated sell, so this is a
 * fraction, tunable at runtime via the Custom:NMSLootSellPercent rule (default
 * 25%). Shared by the payout and by the value shown in the client's Pending tab,
 * so they agree.
 */
static uint64 NMSSellPayout(const EQ::ItemData *item, uint16 charges, uint16 copies)
{
	const int    pct   = RuleI(Custom, NMSLootSellPercent);
	const uint64 gross = item ? (static_cast<uint64>(item->Price) * charges * copies) : 0;
	return (gross * static_cast<uint64>(pct < 0 ? 0 : pct)) / 100;
}

/*
 * NMS-LOCAL, Triptych only: place one EXACT item instance.
 *
 * Builds the instance itself rather than calling SummonItemIntoInventory, so
 * Custom:DoItemUpgrades can never rewrite item_id on the way back to the player.
 *
 * try_cursor = true lets AutoPutLootInInventory fall back to the cursor via
 * FindFreeSlot, which is the behaviour we want: a real slot when there is room,
 * the cursor when there is not.
 *
 * The cursor is a FIFO QUEUE, not a single slot -- ItemInstQueue m_cursor
 * (inventory_profile.h:254) -- and every cursor API APPENDS to it through
 * InventoryProfile::PushCursor(), which clones (inventory_profile.cpp:259). So an
 * already-occupied cursor is never overwritten by a returned item; the existing
 * item stays at the front and the returned one queues behind it.
 *
 * Ownership: AutoPutLootInInventory, PutLootInInventory and PushItemOnCursor all
 * take a const reference and clone, so the instance built here is always ours to
 * delete. Nothing in this path takes ownership, and no pointer to `inst` outlives
 * the delete below.
 */
bool Client::ReturnNMSLootItemExact(uint32 item_id, uint16 charges)
{
	if (!item_id) {
		return false;
	}

	EQ::ItemInstance *inst = database.CreateItem(item_id, static_cast<int16>(charges));
	if (!inst) {
		LogError("[NMSLoot] CreateItem failed for item [{}] on [{}]", item_id, GetName());
		return false;
	}

	if (!AutoPutLootInInventory(*inst, false, true)) {
		// Belt and braces. With try_cursor=true this is unreachable in practice:
		// InventoryProfile::FindFreeSlot returns invslot::slotCursor unconditionally
		// when it runs out of bag space -- "Always room on cursor (it's a queue)"
		// (inventory_profile.cpp:1022). It stays because a fallback that silently
		// dropped an item the player is owed would be far worse than a redundant one.
		//
		// Both branches APPEND to the cursor queue. Neither replaces nor deletes the
		// item already there:
		//   - cursor empty  : PutLootInInventory(slotCursor) -> m_inv.PushCursor
		//   - cursor occupied: PushItemOnCursor                -> m_inv.PushCursor
		// PutItemInInventory routes slotCursor to PushItemOnCursor too
		// (inventory.cpp:1246), so both are the same established mechanism.
		//
		// client_update is deliberately false: the appended item sits BEHIND the
		// queue front and is not visible until the front is moved off, and
		// PutLootInInventory likewise sends nothing for a subordinate cursor item on
		// RoF+ (inventory.cpp:1291) to avoid overwriting the visible cursor and
		// desyncing the client.
		if (m_inv.CursorEmpty()) {
			PutLootInInventory(EQ::invslot::slotCursor, *inst);
			LogInfo("[NMSLoot] inventory full for [{}] on [{}] - placed exact item on empty cursor", item_id, GetName());
		} else {
			if (!PushItemOnCursor(*inst, false)) {
				// The clone is already queued in m_cursor; only the DB write failed, so
				// there is nothing left to free and nothing to retry here. The instance
				// below is still ours and is deleted normally.
				LogError(
					"[NMSLoot] cursor queue SaveCursor failed for item [{}] on [{}] - item is queued but not persisted",
					item_id, GetName()
				);
			}
			LogInfo(
				"[NMSLoot] inventory and cursor both busy for [{}] on [{}] - queued exact item behind cursor item",
				item_id, GetName()
			);
		}
	}

	delete inst;
	return true;
}

/*
 * NMS-LOCAL, Triptych only: place one EXACT item instance into a SPECIFIC slot.
 *
 * Used for the Bank action, which must land in a main bank slot. Main bank
 * slots only -- _PutItem() (inventory_profile.cpp) routes anything outside
 * [BANK_BEGIN, BANK_END] to a bag lookup, and inventing bag-slot banking
 * semantics here would be guesswork. Callers must have already confirmed the
 * slot is free and inside the bank range.
 */
bool Client::PlaceNMSLootItemExact(int16 to_slot, uint32 item_id, uint16 charges)
{
	if (!item_id || to_slot < EQ::invslot::BANK_BEGIN || to_slot > EQ::invslot::BANK_END) {
		LogError("[NMSLoot] PlaceNMSLootItemExact refused: item [{}] slot [{}] for [{}]",
			item_id, to_slot, GetName());
		return false;
	}

	EQ::ItemInstance *inst = database.CreateItem(item_id, static_cast<int16>(charges));
	if (!inst) {
		LogError("[NMSLoot] CreateItem failed for item [{}] on [{}]", item_id, GetName());
		return false;
	}

	PutLootInInventory(to_slot, *inst);

	delete inst;
	return true;
}

/*
 * Remember what the player decided for an item, so the same drop can be handled
 * automatically next time without the /nmsloot window being open.
 *
 * This exists because the client only evaluates its own rules while the Pending
 * tab is being rendered. Learning from the player's actual choices is the only
 * way to automate that the server can do honestly: it never invents a decision,
 * it only repeats one the player already made.
 */
void Client::RecordNMSLootRule(uint32 item_id, uint8 action)
{
	// Vault is unsupported and ACK is not a decision -- neither is worth remembering.
	if (action == NMSLOOT_ACTION_ACK || action == NMSLOOT_ACTION_VAULT) {
		return;
	}

	database.QueryDatabase(fmt::format(
		"REPLACE INTO nms_loot_rules (character_id, item_id, action) VALUES ({}, {}, {})",
		CharacterID(), item_id, static_cast<int>(action)
	));
}

uint8 Client::GetNMSLootRule(uint32 item_id)
{
	auto res = database.QueryDatabase(fmt::format(
		"SELECT action FROM nms_loot_rules WHERE character_id = {} AND item_id = {} LIMIT 1",
		CharacterID(), item_id
	));
	if (!res.Success()) {
		return 0;
	}
	for (auto row = res.begin(); row != res.end(); ++row) {
		return static_cast<uint8>(atoi(row[0]));
	}
	return 0;
}

/*
 * Record an offer WITHOUT transmitting. Every send carries the full pending list
 * (the client replaces rather than appends), so offering N items to M players one
 * call at a time meant N*M packets, each larger than the last. Queue them, then
 * call SendNMSLootPending() once.
 *
 * offer_id is the client's dedup key, but it is deliberately ignored: the row id
 * is always the item id (see below). The parameter exists so the Perl binding
 * signature stays stable.
 */
void Client::QueueNMSLootOffer(const char *from_player, uint32 item_id, const char *item_name, uint32 offer_id, uint16 charges)
{
	(void) offer_id;

	if (!from_player || !item_name || !item_id) {
		return;
	}

	// ONE ROW PER ITEM, ALWAYS -- the offer id IS the item id.
	//
	// The client writes its rules file as `Name=Action|icon|<offer id>` and matches
	// saved rules by that id, so the id must be stable per item or no rule can match
	// a later drop. A previous BFE attempt gave a second copy of the same item a
	// disambiguated id (item_id + n*100,000,000); that preserved the item but let
	// one item end up with two rules that could disagree.
	//
	// Instead a repeat folds into the existing row: stackables add charges,
	// everything else increments `copies`. One row, one rule, one decision --
	// applied to every copy that row stands for.
	//
	// The client gets FIRST crack at every item -- nothing is pre-empted here.
	// Whatever it fails to apply is picked up later by SweepNMSLootOffers().
	const EQ::ItemData *idata = database.GetItem(item_id);
	const uint16        add_c = charges > 0 ? charges : 1;

	auto existing = m_nms_offers.find(item_id);
	if (existing != m_nms_offers.end()) {
		const int cap      = (idata && idata->StackSize > 0) ? idata->StackSize : 1;
		const int combined = static_cast<int>(existing->second.charges) + static_cast<int>(add_c);
		if (idata && idata->Stackable && combined <= cap) {
			existing->second.charges = static_cast<uint16>(combined);
			LogInfo("[NMSLoot] merged [{}] into offer for [{}], now x{}", item_id, GetName(), combined);
		} else if (existing->second.copies < 0xFFFF) {
			existing->second.copies++;
			LogInfo(
				"[NMSLoot] extra copy of [{}] folded into one row for [{}], now {} copies",
				item_id, GetName(), existing->second.copies
			);
		} else {
			// Counter saturated (not reachable in practice). Return the item rather
			// than let it evaporate.
			ReturnNMSLootItemExact(item_id, add_c);
			LogError("[NMSLoot] copies saturated for item [{}] on [{}] - returned directly", item_id, GetName());
		}
		return;
	}

	// Evict the genuinely OLDEST entry. The map is keyed by item id, so begin() is
	// the lowest item id -- which has nothing to do with age -- hence the scan for
	// the smallest seq rather than taking begin().
	while (m_nms_offers.size() >= NMSLOOT_MAX_OFFERS) {
		auto oldest = m_nms_offers.begin();
		for (auto i = m_nms_offers.begin(); i != m_nms_offers.end(); ++i) {
			if (i->second.seq < oldest->second.seq) {
				oldest = i;
			}
		}
		// Hand it back rather than erase it. An offered item was REMOVED from the
		// corpse, so the pending entry is the only copy in existence -- dropping it
		// destroyed the item outright, silently, purely because the player let 64
		// items pile up. Returning it is the only non-destructive option.
		const uint16 ev_copies = oldest->second.copies > 0 ? oldest->second.copies : 1;
		for (uint16 c = 0; c < ev_copies; ++c) {
			ReturnNMSLootItemExact(oldest->second.item_id, oldest->second.charges);
		}
		Message(
			Chat::Yellow,
			"[Loot] Pending list full - %s was returned to you to make room.",
			oldest->second.item_name
		);
		LogInfo(
			"[NMSLoot] pending list full for [{}], returned oldest offer item [{}] x{} ({} copies)",
			GetName(), oldest->second.item_id, oldest->second.charges, ev_copies
		);
		m_nms_offers.erase(oldest);
	}

	NMSPendingOffer rec{};
	rec.item_id = item_id;
	rec.charges = add_c;
	rec.copies  = 1;
	rec.seq     = ++m_nms_offer_seq;
	rec.queued_at = Timer::GetCurrentTime();
	strn0cpy(rec.item_name, item_name, sizeof(rec.item_name));
	strn0cpy(rec.from_player, from_player, sizeof(rec.from_player));
	m_nms_offers[item_id] = rec;

	// Event-driven, not polled: the sweep only runs while something is pending.
	if (!m_nms_sweep_timer.Enabled()) {
		m_nms_sweep_timer.Start();
	}

	// Log each item as it is queued. Without this the only record was "sending N
	// pending item(s)", so offer->decision latency could only be guessed from the
	// nearest kill.
	LogInfo(
		"[NMSLoot] queued [{}] ({}) x{} for [{}]",
		item_name, item_id, add_c, GetName()
	);
}

/*
 * Apply remembered decisions the CLIENT did not apply.
 *
 * The client evaluates its own loot rules only as an item ARRIVES, and over a
 * large sample it missed a substantial fraction of them -- with a rule present
 * and correct every time. A missed item is then never re-evaluated, because
 * re-sending the pending list does not count as a new arrival, so it strands
 * there until the player acts by hand.
 *
 * Deliberately a SAFETY NET, not a replacement: the client is given
 * NMS_SWEEP_GRACE_MS to act first, and the server only ever repeats a decision
 * the player has already made for that exact item.
 */
static const uint32 NMS_SWEEP_GRACE_MS = 20000;   // 20s for the client to have its go

void Client::SweepNMSLootOffers()
{
	if (m_nms_offers.empty()) {
		m_nms_sweep_timer.Disable();
		return;
	}

	if (!RuleB(Custom, NMSLootRememberDecisions)) {
		return;
	}

	const uint32 now = Timer::GetCurrentTime();

	// Collect first: ApplyNMSLootDecision can modify the map.
	std::vector<std::pair<uint32, uint8>> to_apply;
	for (const auto &kv : m_nms_offers) {
		if (now - kv.second.queued_at < NMS_SWEEP_GRACE_MS) {
			continue;
		}
		const uint8 remembered = GetNMSLootRule(kv.second.item_id);
		if (remembered != 0) {
			to_apply.emplace_back(kv.first, remembered);
		}
	}

	for (const auto &entry : to_apply) {
		auto it = m_nms_offers.find(entry.first);
		if (it == m_nms_offers.end()) {
			continue;
		}
		const uint32 item_id = it->second.item_id;
		const uint16 charges = it->second.charges;
		const uint16 copies  = it->second.copies > 0 ? it->second.copies : 1;
		const EQ::ItemData *idata = database.GetItem(item_id);

		LogInfo(
			"[NMSLoot] SWEEP: client missed [{}] ({}) after {}ms - applying remembered action [{}] for [{}]",
			idata ? idata->Name : "?", item_id, NMS_SWEEP_GRACE_MS, entry.second, GetName()
		);

		m_nms_offers.erase(it);
		ApplyNMSLootDecision(item_id, charges, copies, entry.second);
	}

	if (!to_apply.empty()) {
		SendNMSLootPending();
	}
	if (m_nms_offers.empty()) {
		m_nms_sweep_timer.Disable();
	}
}

/*
 * Transmit the complete outstanding pending list.
 */
void Client::SendNMSLootPending()
{
	// The client REPLACES its pending list with whatever a 0x140A packet carries --
	// it does not append. Sending one item at a time therefore wiped the previous
	// row. So every send transmits the complete outstanding set, which is what the
	// count field and entry array were always for.
	//
	// A zero-entry packet is sent deliberately rather than skipped. The client
	// REPLACES its pending list on every 0x140A, so an empty one is the only way to
	// clear it. Without this, FlushNMSLootOffers() handed the items back but left
	// the rows on screen -- clicking a ghost row then hit the "unknown offer_id"
	// path and did nothing, which looks exactly like the item was eaten.
	size_t count = m_nms_offers.size();
	if (count > NMSLOOT_MAX_OFFERS) {
		count = NMSLOOT_MAX_OFFERS;
	}

	const size_t packet_size = sizeof(NMSLootOffer_Struct) + (count * sizeof(NMSLootEntry_Struct));
	auto outapp = new EQApplicationPacket(OP_NMSLootOffer, static_cast<uint32>(packet_size));
	// BasePacket(op, nullptr, len) already zero-fills when buf == nullptr.
	auto *o = reinterpret_cast<NMSLootOffer_Struct *>(outapp->pBuffer);

	o->field_00 = 0;
	if (!m_nms_offers.empty()) {
		strn0cpy(o->header_name, m_nms_offers.begin()->second.from_player, sizeof(o->header_name));
	}
	o->count = static_cast<uint32>(count);

	const int icon_off = RuleI(Custom, NMSLootIconOffset);

	uint32 i = 0;
	for (const auto &kv : m_nms_offers) {
		if (i >= count) {
			break;
		}
		auto &e = o->entries[i];
		e.id       = kv.first;
		e.item_ref = kv.second.item_id;
		strn0cpy(e.item_name, kv.second.item_name, sizeof(e.item_name));
		strn0cpy(e.from_player, kv.second.from_player, sizeof(e.from_player));

		// Entry +0x04 is the ICON id. Established by probe in BFE: writing the item's
		// real icon there rendered the correct graphic, and writing a different value
		// rendered that value's graphic instead -- so the field is consumed as an
		// icon id, not an item id. The parse routine never reads it, which is why it
		// took a live probe to find: it is used by the render path.
		// The rule can still override the offset for further experiments; 0 keeps
		// the proven default.
		// +0x08 is the SELL VALUE in copper -- what choosing Sell pays for this row.
		// The Triune client never reads this field, so it is free to use.
		{
			const uint64 payout = NMSSellPayout(database.GetItem(kv.second.item_id),
			                                    kv.second.charges, kv.second.copies);
			const uint32 value  = payout > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32>(payout);
			memcpy(reinterpret_cast<uint8 *>(&e) + 0x08, &value, sizeof(value));
			// Flag bit 1 at +0x15 says "+0x08 is a sell value". Servers without this
			// change send the item id there, and the client must not show that as a
			// price. Triune reads only bit 0 (LORE) of this byte.
			reinterpret_cast<uint8 *>(&e)[0x15] |= 0x02;
		}

		// Entry +0x10 is the BONUS flag -- writing here renders "[BONUS]" on the row
		// and is what the "Auto-keep all bonus loot" setting acts on. This fork
		// encodes upgraded loot as base_id + tier * 1,000,000 (NPC::DoUpgradeLoot),
		// and the nms DB really does carry ~64k items in each tier band, so an id at
		// or above 1,000,000 IS an upgraded drop. Flagging those makes the setting
		// mean something instead of never firing.
		if (kv.second.item_id >= 1000000) {
			const uint8 bonus = 1;
			memcpy(reinterpret_cast<uint8 *>(&e) + 0x10, &bonus, sizeof(bonus));
		}

		const EQ::ItemData *idata = database.GetItem(kv.second.item_id);

		// Entry +0x11 (u32, unaligned) is the TRIBUTE VALUE. The client refuses the
		// Tribute action when it is 0 ("[NMS Tribute] %s has no tribute value -
		// please pick another option.", dinput8.dll 0x10011ec0), and nothing ever
		// wrote it, so Tribute could never be chosen even though the TRIBUTE branch
		// works. Items with no favor still send 0, which keeps that refusal correct.
		if (idata && idata->Favor > 0) {
			const uint32 favor = static_cast<uint32>(idata->Favor);
			memcpy(reinterpret_cast<uint8 *>(&e) + 0x11, &favor, sizeof(favor));
		}

		if (idata) {
			const uint32 icon    = idata->Icon;
			const int    use_off = (icon_off > 0 &&
			                        icon_off + 4 <= static_cast<int>(sizeof(NMSLootEntry_Struct)))
			                       ? icon_off : 0x04;
			memcpy(reinterpret_cast<uint8 *>(&e) + use_off, &icon, sizeof(icon));
		}
		++i;
	}

	LogInfo("[NMSLoot] sending [{}] pending item(s) to [{}]", i, GetName());

	QueuePacket(outapp);
	safe_delete(outapp);
}

/*
 * Hand back anything still sitting in the Pending list.
 *
 * Offered items are REMOVED from the corpse, so an undecided offer is the only
 * copy in existence. Logging out or zoning would destroy it. Called on disconnect
 * by the later integration patch so nothing is silently lost -- the player gets
 * the items rather than a mystery.
 */
void Client::FlushNMSLootOffers()
{
	if (m_nms_offers.empty()) {
		m_nms_sweep_timer.Disable();
		return;
	}

	const size_t n = m_nms_offers.size();
	for (const auto &kv : m_nms_offers) {
		const uint16 n_copies = kv.second.copies > 0 ? kv.second.copies : 1;
		for (uint16 c = 0; c < n_copies; ++c) {
			ReturnNMSLootItemExact(kv.second.item_id, kv.second.charges);
		}
	}
	m_nms_offers.clear();

	// Now empty -- stop sweeping, and push the cleared list so the window does not
	// keep showing rows for items the player is already holding.
	m_nms_sweep_timer.Disable();
	SendNMSLootPending();

	Message(Chat::Yellow, "[Loot] %u undecided item(s) returned to you.", static_cast<unsigned>(n));
	LogInfo("[NMSLoot] flushed [{}] undecided offer(s) back to [{}]", n, GetName());
}

/*
 * Send the Box Looting Control roster (opcode 0x140D): the list of characters the
 * /nmsloot window offers as candidates for "active looter". Without it the Looters
 * panel has nothing to show and the Claim button has no targets.
 *
 * Fixed 1284-byte packet regardless of how many names are used -- the client
 * reads a fixed array, so a short packet is rejected outright.
 */
void Client::SendNMSLooterList(const std::vector<std::string> &names)
{
	auto outapp = new EQApplicationPacket(OP_NMSLooterList, sizeof(NMSLooterList_Struct));
	// BasePacket(op, nullptr, len) already zero-fills when buf == nullptr.
	auto *l = reinterpret_cast<NMSLooterList_Struct *>(outapp->pBuffer);

	const size_t n = names.size() < NMSLOOT_MAX_ROSTER ? names.size() : NMSLOOT_MAX_ROSTER;
	l->count = static_cast<uint32>(n);
	for (size_t i = 0; i < n; ++i) {
		strn0cpy(l->names[i], names[i].c_str(), sizeof(l->names[i]));
	}

	LogInfo("[NMSLoot] looter roster: {} name(s) to [{}]", n, GetName());

	QueuePacket(outapp);
	safe_delete(outapp);
}

/*
 * Client acted on an offer (0x140B). The packet is client-supplied and is NOT
 * trusted: it only names which offer was clicked. A grant happens only when that
 * offer id is present in m_nms_offers (i.e. this server really did offer it to
 * this character) AND the item id matches what we recorded. The entry is erased
 * before granting so a replayed reply cannot grant twice.
 */
void Client::Handle_OP_NMSLootReply(const EQApplicationPacket *app)
{
	if (app->size != sizeof(NMSLootReply_Struct)) {
		LogError(
			"[NMSLoot] OP_NMSLootReply wrong size: expected [{}] got [{}] from [{}]",
			sizeof(NMSLootReply_Struct), app->size, GetName()
		);
		return;
	}

	// Read through memcpy: app->pBuffer is not guaranteed to be aligned for the
	// uint32 members of this packed struct.
	NMSLootReply_Struct reply{};
	memcpy(&reply, app->pBuffer, sizeof(reply));

	LogInfo(
		"[NMSLoot] reply from [{}] action [{}] entry_id [{}] item_ref [{}] target [{}]",
		GetName(), reply.action, reply.entry_id, reply.entry_field_0C, reply.from_player
	);

	// action 0 with entry_id 0 / item_ref 0xFFFE arrives about a second after every
	// offer. It is an acknowledgement, not a decision -- acting on it would grant
	// items nobody asked for.
	if (reply.action == NMSLOOT_ACTION_ACK || reply.entry_id == 0) {
		return;
	}

	// Anything outside the documented action set is rejected before the map is
	// touched, so a garbage code can never consume an offer.
	switch (reply.action) {
		case NMSLOOT_ACTION_KEEP:
		case NMSLOOT_ACTION_SELL:
		case NMSLOOT_ACTION_TRIBUTE:
		case NMSLOOT_ACTION_BANK:
		case NMSLOOT_ACTION_VAULT:
		case NMSLOOT_ACTION_DESTROY:
		case NMSLOOT_ACTION_PASS:
			break;
		default:
			LogError(
				"[NMSLoot] unknown action [{}] from [{}] for entry [{}] - ignored",
				reply.action, GetName(), reply.entry_id
			);
			return;
	}

	auto it = m_nms_offers.find(reply.entry_id);
	if (it == m_nms_offers.end()) {
		// Either already handled (no double-grant) or never offered (forged).
		LogInfo("[NMSLoot] ignoring reply for unknown offer_id [{}] from [{}]", reply.entry_id, GetName());
		return;
	}

	const uint32 item_id = it->second.item_id;
	const uint16 charges = it->second.charges;
	// One row can stand for several separate items (a non-stackable dropped twice).
	// Every branch below must honour this or the extras are silently destroyed.
	const uint16 copies  = it->second.copies > 0 ? it->second.copies : 1;

	// Cross-check the item the client claims against what we recorded. A mismatch
	// means a tampered packet, so drop the offer entirely rather than guess -- but
	// the item was removed from the corpse, so it still has to come back.
	if (reply.entry_field_0C != item_id) {
		LogError(
			"[NMSLoot] item mismatch from [{}]: offer [{}] was item [{}], client claimed [{}] - returning it",
			GetName(), reply.entry_id, item_id, reply.entry_field_0C
		);
		m_nms_offers.erase(it);
		for (uint16 c = 0; c < copies; ++c) {
			ReturnNMSLootItemExact(item_id, charges);
		}
		SendNMSLootPending();
		return;
	}

	if (reply.action == NMSLOOT_ACTION_PASS) {
		// Pass the whole row (every copy) to the named player. Not remembered as a
		// rule: a pass is a one-off choice of recipient, not what to do with the item.
		char item_name[sizeof(it->second.item_name)];
		strn0cpy(item_name, it->second.item_name, sizeof(item_name));
		m_nms_offers.erase(it);   // consume first, so a replayed reply cannot pass twice
		PassNMSLootOffer(reply.from_player, item_id, item_name, charges, copies);
		SendNMSLootPending();     // the row is gone from this client's Pending list
		return;
	}

	// Consume the offer BEFORE granting, so a duplicated reply cannot grant twice.
	m_nms_offers.erase(it);

	RecordNMSLootRule(item_id, reply.action);
	ApplyNMSLootDecision(item_id, charges, copies, reply.action);
}

/*
 * Loot action 9: give a pending row to another player as a new offer "from" this one.
 *
 * The recipient must be in this zone -- the offer lives on their Client object, so
 * a player in another zone or offline cannot receive it; the item then comes back
 * here instead of being lost. The recipient's own client decides what to do with it
 * (its ignore list may bounce it straight back, which arrives here as a normal
 * offer).
 */
void Client::PassNMSLootOffer(const char *target_name, uint32 item_id, const char *item_name, uint16 charges, uint16 copies)
{
	char target[NMSLOOT_NAME_LEN];
	strn0cpy(target, target_name ? target_name : "", sizeof(target));
	const EQ::ItemData *item = database.GetItem(item_id);
	const char *shown = item ? item->Name : item_name;

	Client *to = target[0] ? entity_list.GetClientByName(target) : nullptr;
	if (!to || to == this) {
		for (uint16 c = 0; c < copies; ++c) {
			ReturnNMSLootItemExact(item_id, charges);
		}
		if (to == this) {
			Message(Chat::Yellow, "[Loot] You can't pass %s to yourself - it is in your inventory.", shown);
		} else {
			Message(Chat::Red, "[Loot] %s is not in this zone - %s returned to your inventory.",
			        target[0] ? target : "Nobody", shown);
		}
		LogInfo("[NMSLoot] PASS: [{}] to [{}] failed (not in zone), returned to [{}]", item_id, target, GetName());
		return;
	}

	for (uint16 c = 0; c < copies; ++c) {
		to->QueueNMSLootOffer(GetName(), item_id, item_name, 0, charges);
	}
	to->SendNMSLootPending();
	Message(Chat::Yellow, "[Loot] Passed %s to %s.", shown, to->GetName());
	LogInfo("[NMSLoot] PASS: [{}] x{} ({} copies) from [{}] to [{}]", item_id, charges, copies, GetName(), to->GetName());
}

/*
 * Perform one loot decision. Shared by the player's own reply and by a remembered
 * rule applied at loot time, so both paths behave identically.
 */
void Client::ApplyNMSLootDecision(uint32 item_id, uint16 charges, uint16 copies, uint8 action)
{
	const EQ::ItemData *item = database.GetItem(item_id);

	switch (action) {

	case NMSLOOT_ACTION_KEEP:
		// Into a real inventory slot, not the cursor, whenever there is room --
		// having to clear the cursor after every single accept is miserable.
		// ReturnNMSLootItemExact falls back to the cursor only when there is
		// genuinely no room, and never upgrades the item on the way in.
		for (uint16 c = 0; c < copies; ++c) {
			ReturnNMSLootItemExact(item_id, charges);
		}
		LogInfo("[NMSLoot] KEEP: granted [{}] x{} ({} copies) to [{}]", item_id, charges, copies, GetName());
		break;

	case NMSLOOT_ACTION_SELL: {
		// The item never entered inventory, so there is nothing to remove -- just
		// pay out (a fraction of vendor value; see NMSSellPayout).
		const uint64 payout = NMSSellPayout(item, charges, copies);
		if (payout > 0) {
			AddMoneyToPP(payout, true);
			Message(Chat::Yellow, "[Loot] Sold %s for %s.",
			        item ? item->Name : "item", NMSFormatCoin(payout).c_str());
			LogInfo("[NMSLoot] SELL: paid [{}] copper to [{}] for item [{}]", payout, GetName(), item_id);
		} else {
			Message(Chat::Yellow, "[Loot] %s has no sell value - discarded.", item ? item->Name : "Item");
			LogInfo("[NMSLoot] SELL: item [{}] has no value, nothing paid to [{}]", item_id, GetName());
		}
		break;
	}

	case NMSLOOT_ACTION_BANK: {
		// One pass per copy, re-finding a free slot each time -- the previous copy
		// just filled one. Main bank slots only; no bag-slot banking is invented.
		// Falling back to inventory rather than silently dropping the item is
		// deliberate: quietly losing it is what made Bank look broken.
		for (uint16 c = 0; c < copies; ++c) {
			int16 free_slot = INVALID_INDEX;
			for (int16 i = EQ::invslot::BANK_BEGIN; i <= EQ::invslot::BANK_END; ++i) {
				if (!m_inv[i]) {
					free_slot = i;
					break;
				}
			}

			if (free_slot != INVALID_INDEX) {
				PlaceNMSLootItemExact(free_slot, item_id, charges);
				Message(Chat::Yellow, "[Loot] %s banked.", item ? item->Name : "Item");
				LogInfo("[NMSLoot] BANK: [{}] -> bank slot [{}] for [{}]", item_id, free_slot, GetName());
			} else {
				ReturnNMSLootItemExact(item_id, charges);
				Message(Chat::Red, "[Loot] Bank is full - %s placed in your inventory instead.",
				        item ? item->Name : "Item");
				LogInfo("[NMSLoot] BANK: bank full for [{}], item [{}] went to inventory", GetName(), item_id);
			}
		}
		break;
	}

	case NMSLOOT_ACTION_TRIBUTE: {
		// Real tribute, matching live EQ: donating an item grants tribute points
		// equal to its favor value. 115k of the 281k items in this DB carry a favor
		// value, so this is a genuine choice rather than a dead button -- it used to
		// fall through to `default` and simply hand the item back.
		// Scaled by a rule for the same reason selling is: shared loot hands every
		// group member their own copy, so a full group can donate six copies of one
		// drop. Defaults to 100 (live EQ behaviour) so it is opt-in, not a silent nerf.
		const int    tpct  = RuleI(Custom, NMSLootTributePercent);
		const uint64 gross_favor = item ? (static_cast<uint64>(item->Favor) * charges * copies) : 0;
		const uint32 favor = static_cast<uint32>(
			(gross_favor * static_cast<uint64>(tpct < 0 ? 0 : tpct)) / 100
		);
		if (favor > 0) {
			AddTributePoints(static_cast<int32>(favor));
			Message(Chat::Yellow, "[Loot] Donated %s for %u tribute point%s.",
			        item->Name, favor, favor == 1 ? "" : "s");
			LogInfo("[NMSLoot] TRIBUTE: [{}] granted [{}] points to [{}]", item_id, favor, GetName());
		} else {
			// No favor value -- returning it is the only correct outcome. Consuming
			// it would destroy the item for nothing.
			for (uint16 c = 0; c < copies; ++c) {
				ReturnNMSLootItemExact(item_id, charges);
			}
			Message(Chat::Red, "[Loot] %s has no tribute value - returned to your inventory.",
			        item ? item->Name : "Item");
			LogInfo("[NMSLoot] TRIBUTE: item [{}] has no favor, returned to [{}]", item_id, GetName());
		}
		break;
	}

	case NMSLOOT_ACTION_DESTROY:
		// Correct as a no-op: the item was never granted, so not granting it IS
		// destroying it.
		LogInfo("[NMSLoot] DESTROY: discarded [{}] for [{}]", item_id, GetName());
		break;

	case NMSLOOT_ACTION_VAULT: {
		// Stored by the vault's own quest code, so loot follows exactly the rules of a
		// hand deposit (never-store items, bags filled first, pages 1-6, "full"
		// message): each copy goes on the cursor and "#vault_lootstash"
		// (global_player.pl -> VaultStash) stores whatever is on the cursor. The
		// stash reads the cursor, so this only runs while the player's own cursor is
		// empty; anything not stored goes to inventory.
		//
		// Triptych adaptation: the cursor placement uses PlaceNMSLootItemExact, not
		// SummonItem, so a vaulted item cannot be silently upgraded on the way to the
		// cursor.
		uint16 c           = 0;
		bool   cursor_busy = false;
		for (; c < copies; ++c) {
			if (!m_inv.CursorEmpty()) {
				cursor_busy = true;   // the player's own item: never stash that by mistake
				break;
			}
			if (!PlaceNMSLootItemExact(EQ::invslot::slotCursor, item_id, charges)) {
				break;
			}
			parse->EventPlayer(EVENT_SAY, this, "#vault_lootstash", 0);
			if (!m_inv.CursorEmpty()) {
				// Refused or full -- the vault has already said why. Take it back off the
				// cursor; it and the remaining copies go to inventory below.
				DeleteItemInInventory(EQ::invslot::slotCursor, 0, true);
				break;
			}
		}
		const uint16 stored = c;
		for (; c < copies; ++c) {
			ReturnNMSLootItemExact(item_id, charges);
		}
		if (cursor_busy) {
			Message(Chat::Red, "[Loot] Your cursor is holding something, so %s went to your inventory instead of the vault.",
			        item ? item->Name : "the item");
		}
		LogInfo("[NMSLoot] VAULT: [{}] x{}, stored {} of {} copies for [{}]", item_id, charges, stored, copies, GetName());
		break;
	}

	default:
		// Unknown decision: return the item rather than lose it.
		for (uint16 c = 0; c < copies; ++c) {
			ReturnNMSLootItemExact(item_id, charges);
		}
		Message(Chat::Red, "[Loot] Unhandled loot action %u - %s returned to your inventory.",
		        action, item ? item->Name : "Item");
		LogError("[NMSLoot] unhandled action [{}] from [{}] for item [{}]", action, GetName(), item_id);
		break;
	}
}