/**
 * @file nms_loot_protocol.h
 * @brief NMS-LOCAL: 0x140A / 0x140B / 0x140D parsing, cached loot state, and the
 *        outbound loot-reply helper.
 *
 * Protocol/state only. The Pending-list and Roster UIs are Patch 9; nothing here
 * draws anything.
 *
 * WIRE LAYOUTS ARE FROZEN AND MATCHED AGAINST THE SERVER, not against a client
 * that no longer exists. The authority is Release-NMS-Server/zone/nms_lootoffer.cpp
 * (Patch 5), whose struct definitions and static_asserts are mirrored here:
 *
 *   offer header 0x56 (86)   entry 0x96 (150)   reply 75   roster 0x504 (1284)
 *
 * A subtle but important point about the reply size. The original DLL built a
 * 77-byte buffer that included its own opcode at offset 0, because it wrote the
 * opcode into the buffer itself. This DLL's SendEQMessage() PREPENDS the opcode:
 *
 *   eqgame.cpp :: SendEQMessage()
 *       std::vector<char> pkt(Length + 2);
 *       uint16_t opcode = (uint16_t)PacketType;
 *       memcpy(pkt.data(), &opcode, 2);
 *       memcpy(pkt.data() + 2, pData, Length);
 *
 * so we must pass exactly 75 bytes of payload and let the transport add the
 * two opcode bytes. The server's handler confirms the result:
 *
 *   nms_lootoffer.cpp :: Handle_OP_NMSLootReply()
 *       if (app->size != sizeof(NMSLootReply_Struct)) { ... return; }
 *       struct NMSLootReply_Struct { ... };  // 75 bytes
 *
 * app->size is the payload after opcode removal, i.e. 75. Sending 77 here would
 * be silently rejected as a wrong-size packet.
 *
 * DOUBLE-ACTION PROTECTION: acting on a row immediately marks it submitted in the
 * local model. A second click before the server's authoritative refresh arrives
 * is refused locally, so a dropped or slow reply cannot be re-sent and cannot
 * double-grant or double-discard an item.
 */

#pragma once

#include "mod_interface.h"

#include <cstdint>
#include <string>
#include <vector>

// Frozen server values (nms_lootoffer.cpp).
static const uint32_t NMS_LOOT_OFFER  = 0x140A;
static const uint32_t NMS_LOOT_REPLY  = 0x140B;
static const uint32_t NMS_LOOT_ROSTER = 0x140D;

static const size_t NMS_LOOT_OFFER_HEADER_SIZE = 0x56;  // 86
static const size_t NMS_LOOT_ENTRY_SIZE        = 0x96;  // 150
static const size_t NMS_LOOT_REPLY_SIZE        = 75;
static const size_t NMS_LOOT_ROSTER_SIZE      = 0x504; // 1284
// Protocol ceilings. Both are the SERVER's, not arbitrary client limits, and
// both are treated as reject-not-clamp conditions: the server clamps before it
// writes (NMSLOOT_MAX_OFFERS / NMSLOOT_MAX_ROSTER in nms_lootoffer.cpp), so a
// conforming server can never emit more, and a larger value means the packet is
// malformed. Truncating it would hide exactly the disagreement worth catching.
static const size_t NMS_LOOT_MAX_ENTRIES       = 64;   // nms_lootoffer.cpp NMSLOOT_MAX_OFFERS
static const size_t NMS_LOOT_MAX_ROSTER_NAMES  = 20;   // nms_lootoffer.cpp NMSLOOT_MAX_ROSTER
static const size_t NMS_LOOT_NAME_LEN          = 64;

// Decision codes, matching nms_lootoffer.cpp exactly.
enum NmsLootAction {
    NMS_LOOT_ACK     = 0,
    NMS_LOOT_KEEP    = 1,
    NMS_LOOT_SELL    = 2,
    NMS_LOOT_TRIBUTE = 3,
    NMS_LOOT_BANK    = 4,
    NMS_LOOT_VAULT   = 5,
    NMS_LOOT_DESTROY = 6,
    NMS_LOOT_PASS    = 9
};

#pragma pack(push, 1)

// 0x140A -- 86-byte header followed by `count` fixed 150-byte entries.
// Every member is 1-byte aligned in the packed struct; the packet buffer is not
// guaranteed to be aligned either, so the parser uses memcpy for all of them.
struct NmsLootOfferHeader {
    uint16_t field_00;          // 0x00  echoed into the reply's header field
    char     header_name[64];   // 0x02
    uint32_t unknown_42;        // 0x42
    uint32_t unknown_46;        // 0x46
    uint32_t unknown_4A;        // 0x4A
    uint32_t unknown_4E;        // 0x4E
    uint32_t count;             // 0x52
};

// 0x96 bytes. Fields the server documents as consumed are named; the rest are
// kept because Patch 9 renders them (icon, sell value, bonus flag, tribute).
struct NmsLootEntry {
    uint32_t id;                // 0x00  offer id, echoed as reply.entry_id
    uint8_t  unknown_04[8];     // 0x04
    uint32_t item_ref;          // 0x0C  echoed as reply.entry_field_0C
    uint8_t  unknown_10[6];     // 0x10
    char     item_name[64];     // 0x16
    char     from_player[64];   // 0x56  entry skipped server-side if empty
};

// 0x140D -- exactly 0x504 bytes, always sent at full size.
struct NmsLooterList {
    uint32_t count;             // 0x00
    char     names[20][64];     // 0x04
};

// 0x140B payload -- exactly 75 bytes, opcode supplied by SendEQMessage.
struct NmsLootReply {
    uint16_t header_field_00;   // 0x00  echoed from the offer header
    uint32_t entry_field_0C;    // 0x02  echoed from entry + 0x0C
    uint32_t entry_id;          // 0x06  echoed from entry + 0x00
    uint8_t  action;            // 0x0A
    char     from_player[64];   // 0x0B
};

#pragma pack(pop)

// Compile-time agreement with the server's frozen layouts.
static_assert(sizeof(NmsLootOfferHeader) == NMS_LOOT_OFFER_HEADER_SIZE,
              "0x140A header must be 0x56 bytes to match the server");
static_assert(sizeof(NmsLootEntry) == NMS_LOOT_ENTRY_SIZE,
              "0x140A entry must be 0x96 bytes to match the server");
static_assert(sizeof(NmsLooterList) == NMS_LOOT_ROSTER_SIZE,
              "0x140D roster must be 0x504 bytes to match the server");
static_assert(sizeof(NmsLootReply) == NMS_LOOT_REPLY_SIZE,
              "0x140B payload must be 75 bytes; SendEQMessage adds the opcode");
static_assert(offsetof(NmsLootOfferHeader, count) == 0x52, "offer count at 0x52");
static_assert(offsetof(NmsLootEntry, id) == 0x00, "entry id at 0x00");
static_assert(offsetof(NmsLootEntry, item_ref) == 0x0C, "entry item_ref at 0x0C");
static_assert(offsetof(NmsLootEntry, item_name) == 0x16, "entry item_name at 0x16");
static_assert(offsetof(NmsLootEntry, from_player) == 0x56, "entry from_player at 0x56");
static_assert(offsetof(NmsLootReply, header_field_00) == 0x00, "reply header at 0x00");
static_assert(offsetof(NmsLootReply, entry_field_0C) == 0x02, "reply item_ref at 0x02");
static_assert(offsetof(NmsLootReply, entry_id) == 0x06, "reply entry_id at 0x06");
static_assert(offsetof(NmsLootReply, action) == 0x0A, "reply action at 0x0A");
static_assert(offsetof(NmsLootReply, from_player) == 0x0B, "reply name at 0x0B");

// One decoded offer row. Fields the server fills at documented offsets are
// typed; the rest are retained raw so Patch 9 can use them without a protocol
// change.
struct NmsLootOfferRow {
    uint32_t id;
    uint32_t item_ref;
    uint32_t header_field_00;
    std::string item_name;
    std::string from_player;
    uint32_t icon;              // entry + 0x04
    uint32_t sell_value;        // entry + 0x08
    uint8_t  bonus_flag;        // entry + 0x10
    uint32_t tribute_value;     // entry + 0x11
    bool     submitted;         // local double-action guard

    NmsLootOfferRow()
        : id(0), item_ref(0), header_field_00(0), icon(0), sell_value(0),
          bonus_flag(0), tribute_value(0), submitted(false) {}
};

// Which accepted protocol event changed the loot model.
//
// Declared at namespace scope because both translation units need it: the protocol
// layer names the event, and the window switches on it. It is NOT inferred from
// model state -- Pending() stays non-empty after the user closes the window, so a
// size test would reopen a window they deliberately dismissed.
enum NmsLootChange {
    NMS_LOOT_CHANGED_PENDING_NEW,    // valid non-empty 0x140A: may create + show
    NMS_LOOT_CHANGED_PENDING_CLEAR,  // zero-count 0x140A, or a local reply we sent
    NMS_LOOT_CHANGED_ROSTER          // valid 0x140D: refresh an existing window only
};

class NmsLootProtocol : public IMod {
public:
    const char* GetName() const override;
    bool        Initialize()    override;
    void        Shutdown()      override;

    void        OnSetGameState(int gameState) override;

    // IMod packet hook. Returns true for every opcode except the NMS ones it
    // consumes, so unrelated traffic is untouched.
    bool        OnIncomingMessage(uint32_t opcode, const void* buffer, uint32_t size) override;

    // ---- cached model (Patch 9 reads these) --------------------------------
    const std::vector<NmsLootOfferRow>& Pending() const { return m_pending; }
    const std::vector<std::string>&     Roster()   const { return m_roster; }
    uint16_t LastHeaderField() const { return m_last_header_field; }

    const NmsLootOfferRow* FindById(uint32_t id) const;

    // ---- outbound ----------------------------------------------------------
    // Builds the frozen 75-byte reply and sends it on 0x140B. `target` is the
    // pass recipient; ignored for every action except NMS_LOOT_PASS, matching
    // the server, which reads from_player only on the pass branch.
    //
    // Returns false (and sends nothing) when the id is unknown or already
    // submitted. On success the row is marked submitted BEFORE the send, so a
    // re-entrant or immediate second click cannot produce a second packet.
    bool SendReply(uint32_t offer_id, uint8_t action, const char* target = nullptr);

    void        ClearPending();
    void        ClearRoster();
    void        ClearAll();

    static NmsLootProtocol* GetInstance();

private:
    bool HandleOffer(const void* buffer, uint32_t size);
    bool HandleRoster(const void* buffer, uint32_t size);

    static NmsLootProtocol* s_instance;

    std::vector<NmsLootOfferRow> m_pending;
    std::vector<std::string>     m_roster;
    uint16_t                     m_last_header_field;

    NmsLootOfferRow m_missing_row; // returned for unknown-id lookups
};