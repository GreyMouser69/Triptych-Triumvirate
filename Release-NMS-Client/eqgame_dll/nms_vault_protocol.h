/**
 * @file nms_vault_protocol.h
 * @brief NMS-LOCAL: VAULTDATA protocol parsing and cached vault state.
 *
 * Protocol/state only. This is NOT the vault window -- that is Patch 8. Nothing
 * here draws anything; it owns the parsed model that a later patch renders.
 *
 * GRAMMAR IS NOT GUESSED. Every field below was read out of the authoritative
 * server emitter, Release-NMS-Plugins/NMS_vault_utils.pl :: VaultRender():
 *
 *   my @burst = ("VAULTDATA|CLEAR", "VAULTDATA|OPEN|$page");
 *   push @burst, join('|', "VAULTDATA|ADD", $page, $r->{slot}, $r->{item_id},
 *                      $qty, $icon, $name, $contents,
 *                      map({ $r->{$_} || 0 } qw(augment_one augment_two augment_three
 *                                               augment_four augment_five augment_six)),
 *                      0, $cap);
 *
 * i.e. '|'-joined, 17 tokens total:
 *
 *   [0] VAULTDATA   [1] ADD
 *   [2] page        [3] global slot   [4] item id   [5] qty/charges
 *   [6] icon        [7] item name     [8] bag contents (nested, may be empty)
 *   [9..14] augment ids 1-6
 *   [15] unread flag (always 0)      [16] bag capacity (0 = not a bag)
 *
 * TRANSFORM: delivered as NPC speech, not a tell. Client::NPCSayTo builds the
 * identical OP_FormattedMessage that Mob::Say produces (GENERIC_SAY), and
 * NMS_vault_utils.pl documents that the transport is a real NPC talking --
 * $client->Message() variants were tested and produce nothing the DLL sees.
 * So the payload arrives inside the rendered chat line, wrapped in the NPC
 * speech quotes. The parser therefore locates the "VAULTDATA" token in the line
 * and reads to the closing quote, rather than assuming a fixed column.
 *
 * TEXT SAFETY: VaultCleanName() strips every delimiter from item names
 * ($name =~ s/[|:~]//g) and truncates to 127 chars, so neither the '|' field
 * separator nor the nested ':' / '~' pair can appear inside a name. That is what
 * makes a deterministic split safe, and it is why the split is safe here.
 *
 * TRAILING-QUOTE ARTIFACT: NPC speech wraps the body in quotes, so the closing
 * quote lands on the LAST field unless it is removed first. The emitter
 * anticipates this -- "Sending all 15 also keeps the NPC speech's closing quote
 * off the item name: it lands on field 14, which atoi ignores" -- which is why it
 * always sends all 15 fields.
 *
 * OnChatText() isolates the speech body between the opening and closing quotes
 * BEFORE calling HandlePayload(), so the framing quote never becomes part of any
 * field. Every numeric field, bag capacity (16) included, is therefore parsed
 * strictly. No field tolerates arbitrary trailing junk.
 *
 * COMPATIBILITY: the original DLL's parser (va 0x1019943b, documented in
 * BFE docs/NMSLOOT_PROTOCOL.md section 3c) discarded any ADD with fewer than 6
 * fields. 17 tokens satisfies that, so this grammar and the original client
 * agree. Fields 8 and 9-16 are Triptych extensions layered onto the original
 * 6-field form, not a reinterpretation of it.
 */

#pragma once

#include "mod_interface.h"

#include <cstdint>
#include <string>
#include <vector>

// Slot geometry is GLOBAL, not per-page. From NMS_vault_utils.pl:
//     first = (page - 1) * 10 + 1
//     last  = (page == 9) ? first + 2 : page * 10
//   page 1 -> 1..10   page 6 -> 51..60
//   page 7 -> 61..70  (Clicky 1)
//   page 8 -> 71..80  (Clicky 2)
//   page 9 -> 81..83   (Proc Locker: Pri / Sec / Rng)
// The server stores the GLOBAL number, so the page is always derivable from it.
static const int NMS_VAULT_FIRST_SLOT   = 1;
static const int NMS_VAULT_LAST_SLOT    = 83;
static const int NMS_VAULT_SLOT_COUNT   = 83;
static const int NMS_VAULT_PAGE_MIN     = 1;
static const int NMS_VAULT_PAGE_MAX     = 9;
static const int NMS_VAULT_PAGE_GENERAL_MAX = 6;  // pages 1-6 general
static const int NMS_VAULT_PAGE_CLICKY_FIRST = 7; // pages 7-8 clicky
static const int NMS_VAULT_PAGE_CLICKY_LAST  = 8;
static const int NMS_VAULT_PAGE_SPECIAL  = 9;     // 81-83

// One bag item inside a container's "contents" field:
//   <item_id>:<charges>:<icon>:<bag_slot>:<name>   joined by '~'
struct NmsVaultBagItem {
    uint32_t    item_id;
    uint32_t    charges;
    uint32_t    icon;
    uint32_t    bag_slot;
    std::string name;

    NmsVaultBagItem() : item_id(0), charges(1), icon(0), bag_slot(0) {}
};

// One cached vault square. `valid` is false for an empty slot; the slot index in
// the array IS the global slot number, so no separate slot field is stored.
struct NmsVaultSlot {
    bool                       valid;
    int                        page;
    uint32_t                   item_id;
    uint32_t                   qty;
    uint32_t                   icon;
    std::string                name;
    std::string                contents;
    uint32_t                   augments[6];
    uint32_t                   unread;
    uint32_t                   bag_cap;
    std::vector<NmsVaultBagItem> bag_items;

    NmsVaultSlot()
        : valid(false), page(0), item_id(0), qty(1), icon(0),
          unread(0), bag_cap(0) {
        for (int i = 0; i < 6; ++i) augments[i] = 0;
    }
};

// Which accepted VAULTDATA verb changed the model.
//
// Declared here, at namespace scope, because both translation units need it: the
// protocol layer names the verb it just accepted, and the window switches on it.
// It is deliberately NOT inferred from IsOpen() at the call site -- an ADD or
// CLEAR after a previously closed window also leaves IsOpen() true, so inference
// would re-show a window the player had closed.
//
// Three values, no dispatch table: this is not a general event framework.
enum NmsVaultChange {
    NMS_VAULT_CHANGED_CLEAR,   // contents emptied; refresh an existing window only
    NMS_VAULT_CHANGED_OPEN,    // page selected; may create and show
    NMS_VAULT_CHANGED_ADD      // one slot written; refresh an existing window only
};

class NmsVaultProtocol : public IMod {
public:
    const char* GetName() const override;
    bool        Initialize()    override;
    void        Shutdown()      override;

    void        OnSetGameState(int gameState) override;

    // Never consumes: VAULTDATA rides on ordinary chat text that other consumers
    // (filters, the client's own printer) must still see, so this returns true
    // unconditionally. Injected from MQ2ChatHook.cpp, not the packet path.
    static void OnChatText(const char* line);

    // ---- model accessors (Patch 8 reads these; nothing renders yet) --------
    bool        IsOpen()  const { return m_open; }
    int         OpenPage() const { return m_open_page; }
    int         SlotCount() const;
    const NmsVaultSlot& Slot(int global_slot) const;
    bool        PageSlotRange(int page, int* first, int* last) const;

    // Drop everything. Used by CLEAR and by every lifecycle reset.
    void        ClearAll();

    static NmsVaultProtocol* GetInstance();

    // Exposed for tests / callers that already hold the payload.
    // Returns true when the line was a VAULTDATA command we acted on.
    bool        HandlePayload(const char* payload, size_t len);

private:
    void        HandleClear();
    bool        HandleOpen(const std::vector<std::string>& fields);
    bool        HandleAdd(const std::vector<std::string>& fields);

    bool        ParseBagContents(const std::string& text,
                                 std::vector<NmsVaultBagItem>* out) const;

    static NmsVaultProtocol* s_instance;

    bool         m_open;
    int          m_open_page;
    NmsVaultSlot m_slots[NMS_VAULT_SLOT_COUNT + 1]; // 1-based; [0] unused

    NmsVaultSlot m_empty_slot; // returned for out-of-range reads
};