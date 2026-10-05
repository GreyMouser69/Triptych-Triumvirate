// NMS-LOCAL: 0x140A / 0x140D parsing, cached loot state, 0x140B reply sender.
// Layout authority: Release-NMS-Server/zone/nms_lootoffer.cpp (Patch 5).

#include "MQ2Main.h"
#include "nms_loot_protocol.h"
#include "nms_loot_window.h"
#include "core_log.h"

#include <cstring>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Model-change notification (Patch 9)
// ---------------------------------------------------------------------------
//
// Mirrors the vault's approach (see nms_vault_protocol.cpp): an explicit reason,
// never inferred from model state.
//
// Inference would be actively wrong here. Patch 8 found that a persistent IsOpen()
// cannot tell "OPEN just arrived" from "an ADD landed while the window was closed".
// The pending-loot equivalent is worse: after a user closes the window Pending() is
// still non-empty, so any "size > 0 means open" test would pop the window back up on
// the next unrelated refresh.
//
// Why may the window OPEN at all? Because the protocol evidence says so: BFE
// docs/NMSLOOT_PROTOCOL.md records that when an offer arrives the client announces
// it and the item lands in the decision queue -- the pending list is a to-decide
// queue, not a passive log. So a newly valid NON-EMPTY packet is an explicit open
// trigger, and nothing else is.
//
// NmsLootChange is declared in nms_loot_protocol.h because the window switches on it.
static void NotifyLootModelChanged(NmsLootChange why) {
    if (why == NMS_LOOT_CHANGED_PENDING_NEW) {
        // The one case that may create and show. A first offer of the session has no
        // window yet and must still bring the decision queue up by itself.
        NmsLootWnd::OnLootModelChanged(NmsLootWnd::CHANGE_PENDING_NEW);
        return;
    }
    // Everything else is refresh-only and is forwarded ONLY when an instance already
    // exists, so a roster update or a background clear can never build the UI.
    if (NmsLootWnd::GetInstance()) {
        switch (why) {
        case NMS_LOOT_CHANGED_ROSTER:
            NmsLootWnd::OnLootModelChanged(NmsLootWnd::CHANGE_ROSTER);
            break;
        default:
            NmsLootWnd::OnLootModelChanged(NmsLootWnd::CHANGE_PENDING_CLEAR);
            break;
        }
    }
}

// Established outbound path for client->server custom opcodes. eqgame.cpp
// prepends the opcode, so `Length` is the payload size only.
extern VOID SendEQMessage(DWORD PacketType, PVOID pData, DWORD Length);

NmsLootProtocol* NmsLootProtocol::s_instance = nullptr;

NmsLootProtocol* NmsLootProtocol::GetInstance() { return s_instance; }

const char* NmsLootProtocol::GetName() const { return "NmsLootProtocol"; }

bool NmsLootProtocol::Initialize() {
    s_instance = this;
    SimpleLog("NmsLootProtocol: Initialized (offer 0x%04X, reply 0x%04X, roster 0x%04X)",
              NMS_LOOT_OFFER, NMS_LOOT_REPLY, NMS_LOOT_ROSTER);
    return true;
}

void NmsLootProtocol::Shutdown() {
    ClearAll();
    s_instance = nullptr;
}

void NmsLootProtocol::ClearPending() {
    m_pending.clear();
    m_last_header_field = 0;
}

void NmsLootProtocol::ClearRoster() {
    m_roster.clear();
}

void NmsLootProtocol::ClearAll() {
    ClearPending();
    ClearRoster();
}

// ---------------------------------------------------------------------------
// Inbound dispatch
// ---------------------------------------------------------------------------
//
// true  -> let the packet continue to the client / other handlers
// false -> consumed
//
// Only the two server->client NMS opcodes are consumed. Anything else returns
// true untouched, which is what keeps this additive: the native client does not
// understand 0x140A/0x140D, so passing them on would only feed unknown opcodes
// to eqgame's dispatcher, and no other handler in this DLL claims them.

bool NmsLootProtocol::OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size) {
    if (opcode == NMS_LOOT_OFFER) {
        HandleOffer(buffer, size);
        return false;
    }
    if (opcode == NMS_LOOT_ROSTER) {
        HandleRoster(buffer, size);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 0x140A -- pending loot offers
// ---------------------------------------------------------------------------

bool NmsLootProtocol::HandleOffer(const void* buffer, uint32_t size) {
    if (!buffer || size < NMS_LOOT_OFFER_HEADER_SIZE) {
        SimpleLog("NmsLoot: offer too small (%u < %u)", size,
                  (unsigned)NMS_LOOT_OFFER_HEADER_SIZE);
        return false;
    }

    // Read the header field-by-field with memcpy: the packet buffer is not
    // guaranteed to satisfy the packed struct's alignment.
    NmsLootOfferHeader hdr;
    std::memset(&hdr, 0, sizeof(hdr));
    std::memcpy(&hdr, buffer, NMS_LOOT_OFFER_HEADER_SIZE);

    const uint32_t declared = hdr.count;

    // (A) Protocol maximum -- REJECT, never clamp.
    //
    // 64 is not an arbitrary client constant. It is the server's own ceiling:
    //   nms_lootoffer.cpp :: SendNMSLootPending()
    //       size_t count = m_nms_offers.size();
    //       if (count > NMSLOOT_MAX_OFFERS) { count = NMSLOOT_MAX_OFFERS; }
    // and NMSLOOT_MAX_OFFERS is 64, hard-coded at the top of that file, with the
    // comment "The client clamps these two itself, but never send more than it
    // will read." The original DLL clamped its own read to 0x40 (64) as well --
    // BFE docs/NMSLOOT_PROTOCOL.md: "entry count, CLAMPED to 0x40 (64)".
    //
    // So 64 is the contract, and a server that sends more is not something to paper
    // over. Silently clamping here would let a malformed packet parse as a valid
    // shorter one, which is precisely the disagreement we want to catch.
    if (declared > NMS_LOOT_MAX_ENTRIES) {
        SimpleLog("NmsLoot: offer rejected, count %u exceeds protocol maximum %u",
                  declared, (unsigned)NMS_LOOT_MAX_ENTRIES);
        return false;
    }

    // (B) Expected size, computed in 64-bit so it cannot wrap. This build is 32-bit
    // (Win32 only: the DLL will not load in a 64-bit client), where
    // `NMS_LOOT_OFFER_HEADER_SIZE + count * NMS_LOOT_ENTRY_SIZE` overflows for a
    // large count -- that is why the arithmetic is widened rather than done in
    // size_t/uint32. Same class of bug as the one already fixed in multi_pet.cpp.
    const uint64_t expected = (uint64_t)NMS_LOOT_OFFER_HEADER_SIZE
                            + ((uint64_t)declared * (uint64_t)NMS_LOOT_ENTRY_SIZE);

    // (C) EXACT agreement, not ">=". Rejects truncated packets, count/size
    // disagreement, and unexplained trailing bytes alike. A 0x140A has no optional
    // tail: the server sizes the buffer as exactly header + count*entry
    // (nms_lootoffer.cpp :: SendNMSLootPending), so anything else is malformed.
    if ((uint64_t)size != expected) {
        SimpleLog("NmsLoot: offer rejected, size %u does not match count %u (expected %llu)",
                  size, declared, (unsigned long long)expected);
        return false;
    }

    const uint32_t count = declared;

    // Build into a local list and swap on success. The packet is authoritative
    // for the whole pending set: rows from an earlier packet are replaced, never
    // appended to, so a row the server has already withdrawn cannot linger.
    std::vector<NmsLootOfferRow> parsed;
    parsed.reserve(count);

    const uint8_t* base = static_cast<const uint8_t*>(buffer) + NMS_LOOT_OFFER_HEADER_SIZE;

    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* src = base + (i * NMS_LOOT_ENTRY_SIZE);

        NmsLootEntry e;
        std::memset(&e, 0, sizeof(e));
        std::memcpy(&e, src, NMS_LOOT_ENTRY_SIZE);

        NmsLootOfferRow row;
        row.id             = e.id;
        row.item_ref       = e.item_ref;
        row.header_field_00 = hdr.field_00;

        // Fixed-width char arrays: bound explicitly, never trust a terminator
        // that the server's zero-fill normally provides but the wire does not
        // guarantee.
        std::memcpy(&row.item_name[0], e.item_name, NMS_LOOT_NAME_LEN);
        row.item_name[NMS_LOOT_NAME_LEN - 1] = '\0';
        std::memcpy(&row.from_player[0], e.from_player, NMS_LOOT_NAME_LEN);
        row.from_player[NMS_LOOT_NAME_LEN - 1] = '\0';

        // Documented field positions. The server writes icon at +0x04, the sell
        // value at +0x08, the bonus flag at +0x10 and the tribute value at +0x11,
        // so they are read from the raw entry rather than the named struct
        // fields (which only cover the echoed and text members).
        std::memcpy(&row.icon, src + 0x04, sizeof(row.icon));
        std::memcpy(&row.sell_value, src + 0x08, sizeof(row.sell_value));
        row.bonus_flag = src[0x10];
        std::memcpy(&row.tribute_value, src + 0x11, sizeof(row.tribute_value));

        parsed.push_back(row);
    }

    m_pending.swap(parsed);
    m_last_header_field = hdr.field_00;
    SimpleLog("NmsLoot: pending set replaced with %u row(s)", (unsigned)count);

    // Announced AFTER the swap, so the window always reads a consistent snapshot.
    // A zero-count packet is the server's explicit "nothing pending" and clears the
    // view; anything non-empty is a new decision queue and opens it.
    NotifyLootModelChanged(count > 0 ? NMS_LOOT_CHANGED_PENDING_NEW
                                     : NMS_LOOT_CHANGED_PENDING_CLEAR);
    return true;
}

// ---------------------------------------------------------------------------
// 0x140D -- looter roster
// ---------------------------------------------------------------------------

bool NmsLootProtocol::HandleRoster(const void* buffer, uint32_t size) {
    // EXACT size. The previous rule was `size >= 0x504`, which accepted an
    // over-long packet and then read a fixed 0x504 out of it, silently ignoring
    // whatever the tail contained.
    //
    // The roster has no optional tail and no variable length: the server always
    // allocates the whole frozen structure.
    //   nms_lootoffer.cpp :: SendNMSLooterList()
    //       auto outapp = new EQApplicationPacket(OP_NMSLooterList,
    //                                             sizeof(NMSLooterList_Struct));
    // and sizeof is 4 + 20*64 = 1284 = 0x504 exactly, asserted by static_assert
    // here and by static_assert on the server. The original client handler
    // demanded ">= 0x504" only because it had to be tolerant of a buffer whose
    // exact length it did not control; here the length is the contract, so
    // anything other than an exact match is malformed.
    if (!buffer || size != NMS_LOOT_ROSTER_SIZE) {
        SimpleLog("NmsLoot: roster rejected, size %u is not the frozen %u",
                  size, (unsigned)NMS_LOOT_ROSTER_SIZE);
        return false;
    }

    uint32_t declared = 0;
    std::memcpy(&declared, buffer, sizeof(declared));

    // REJECT an over-full count rather than truncating it.
    //
    // Checked against Patch 5's writer, which clamps server-side BEFORE it
    // writes:
    //     const size_t n = names.size() < NMSLOOT_MAX_ROSTER ? names.size()
    //                                                       : NMSLOOT_MAX_ROSTER;
    //     l->count = static_cast<uint32>(n);
    // with NMSLOOT_MAX_ROSTER == 20. So a conforming server can never emit a
    // count above 20; seeing one means the packet did not come from our server or
    // was corrupted, and accepting the first 20 rows would hide that.
    if (declared > NMS_LOOT_MAX_ROSTER_NAMES) {
        SimpleLog("NmsLoot: roster rejected, count %u above maximum %u",
                  declared, (unsigned)NMS_LOOT_MAX_ROSTER_NAMES);
        return false;
    }

    const uint32_t count = declared;

    // Deterministic replacement: a new roster fully supersedes the old one, so
    // a departing member cannot linger.
    std::vector<std::string> parsed;
    parsed.reserve(count);

    const char* names = static_cast<const char*>(buffer) + sizeof(uint32_t);
    for (uint32_t i = 0; i < count; ++i) {
        const char* src = names + (i * NMS_LOOT_NAME_LEN);
        // Each name is a fixed 64 bytes; copy then force termination rather than
        // scanning for a NUL that may not be inside the field.
        std::string name(src, strnlen(src, NMS_LOOT_NAME_LEN));
        parsed.push_back(name);
    }

    m_roster.swap(parsed);
    SimpleLog("NmsLoot: roster replaced with %u name(s)", (unsigned)count);

    // Roster arrival never opens the window -- it is supporting data for Pass. It
    // only refreshes an existing window, and NotifyLootModelChanged() enforces that
    // by not forwarding at all when no instance exists.
    NotifyLootModelChanged(NMS_LOOT_CHANGED_ROSTER);
    return true;
}

// ---------------------------------------------------------------------------
// Model accessors
// ---------------------------------------------------------------------------

const NmsLootOfferRow* NmsLootProtocol::FindById(uint32_t id) const {
    for (size_t i = 0; i < m_pending.size(); ++i) {
        if (m_pending[i].id == id) return &m_pending[i];
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// 0x140B -- outbound loot reply
// ---------------------------------------------------------------------------

bool NmsLootProtocol::SendReply(uint32_t offer_id, uint8_t action, const char* target) {
    // Locate the row. Unknown id -> nothing to act on; sending anyway would be a
    // forged reply the server rejects (and, for a valid id already consumed,
    // a replay).
    NmsLootOfferRow* row = nullptr;
    for (size_t i = 0; i < m_pending.size(); ++i) {
        if (m_pending[i].id == offer_id) { row = &m_pending[i]; break; }
    }
    if (!row) {
        SimpleLog("NmsLoot: reply refused, unknown offer id %u", offer_id);
        return false;
    }

    // Double-action guard. Marked BEFORE the send so an immediate second call --
    // from a double-click, or from a UI that fires twice before the server's
    // refresh replaces the set -- is refused locally instead of putting a second
    // grant request on the wire.
    if (row->submitted) {
        SimpleLog("NmsLoot: reply refused, offer %u already submitted", offer_id);
        return false;
    }
    row->submitted = true;

    NmsLootReply reply;
    std::memset(&reply, 0, sizeof(reply));

    // Echo exactly what the server cross-checks (nms_lootoffer.cpp):
    //   reply.entry_id        -> must match a live offer id
    //   reply.entry_field_0C  -> must equal the recorded item id
    reply.header_field_00 = row->header_field_00;
    reply.entry_field_0C  = row->item_ref;
    reply.entry_id        = row->id;
    reply.action          = action;

    // The server reads this field only on the pass branch, where it is the
    // recipient name; echo the offer's sender otherwise so the packet is always
    // well formed.
    const char* name = target;
    if (action != NMS_LOOT_PASS || !name || !*name) {
        name = row->from_player.c_str();
    }
    std::memcpy(&reply.from_player[0], name, NMS_LOOT_NAME_LEN);
    reply.from_player[NMS_LOOT_NAME_LEN - 1] = '\0';

    // 75 bytes of payload. SendEQMessage prepends the 2-byte opcode, giving the
    // 77 bytes on the wire that the server's handler strips back down to 75.
    SendEQMessage((DWORD)NMS_LOOT_REPLY, &reply, (DWORD)sizeof(reply));

    SimpleLog("NmsLoot: reply sent for offer %u action %u", offer_id, (unsigned)action);

    // The row was marked submitted BEFORE the send (see the double-submit guard
    // above), so the view must redraw now rather than wait for the server -- which
    // does not resend the list after an ordinary action. Refresh-only: this must
    // never reopen a window the user closed.
    NotifyLootModelChanged(NMS_LOOT_CHANGED_PENDING_CLEAR);
    return true;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Pending offers and the roster are per-character. A zone change or a character
// switch must not leave the previous character's rows on screen: they refer to
// item ids that no longer exist in the new session, and acting on one would send
// a reply the new character has no offer for.
void NmsLootProtocol::OnSetGameState(int gameState) {
    if (gameState != 5) {  // 5 == GAMESTATE_INGAME
        ClearAll();
    }
}