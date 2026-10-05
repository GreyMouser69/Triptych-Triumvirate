// NMS-LOCAL: VAULTDATA protocol parsing + cached vault state.
// See nms_vault_protocol.h for the full derivation of this grammar from the
// authoritative emitter (Release-NMS-Plugins/NMS_vault_utils.pl).

#include "MQ2Main.h"
#include "nms_vault_protocol.h"
#include "nms_vault_window.h"
#include "core_log.h"

#include <cstring>
#include <cstdlib>

NmsVaultProtocol* NmsVaultProtocol::s_instance = nullptr;

NmsVaultProtocol* NmsVaultProtocol::GetInstance() { return s_instance; }

const char* NmsVaultProtocol::GetName() const { return "NmsVaultProtocol"; }

bool NmsVaultProtocol::Initialize() {
    s_instance = this;
    SimpleLog("NmsVaultProtocol: Initialized");
    return true;
}

void NmsVaultProtocol::Shutdown() {
    ClearAll();
    s_instance = nullptr;
}

void NmsVaultProtocol::ClearAll() {
    m_open = false;
    m_open_page = 0;
    for (int i = 0; i <= NMS_VAULT_SLOT_COUNT; ++i) {
        m_slots[i] = NmsVaultSlot();
    }
}

// Strict unsigned parse: the whole token must be digits and fit in uint32.
// Defined here, above every caller, because HandleOpen uses it too and MSVC will
// not forward-declare a function at namespace scope for us.
static bool ParseUintStrict(const std::string& s, uint32_t* out) {
    if (s.empty() || s.size() > 10) return false;
    uint64_t v = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (uint64_t)(s[i] - '0');
        if (v > 0xFFFFFFFFull) return false;
    }
    *out = (uint32_t)v;
    return true;
}

// ---------------------------------------------------------------------------
// Model-change notification (Patch 8)
// ---------------------------------------------------------------------------
//
// The native vault window is a pure VIEW. Rather than have the window poll or
// re-parse anything, the protocol layer announces that the model moved. This is
// the ONLY direction of travel: the window reads the model, never writes it.
//
// Each command announces AFTER it has fully applied, so the window always sees a
// consistent snapshot.
//
// The reason is passed explicitly rather than left for the window to infer. It has
// to be: the three verbs have different UI-lifetime semantics, and inferring them
// from proto->IsOpen() would be wrong in both directions.
//   - OPEN is the ONLY verb that may create and show the window, so the very first
//     OPEN of a session has to work with no instance yet. Inferring "OPEN" from
//     IsOpen() was the bug this replaces: an ADD or CLEAR after a previously closed
//     window also leaves IsOpen() true, so inferring would re-show a window the
//     player had deliberately closed.
//   - ADD and CLEAR only refresh an ALREADY EXISTING window. They must never
//     instantiate the UI, or vault traffic would build a window nobody asked for.
//
// This is a plain enum, not a general event framework: three values, one function,
// no registration, no dispatch table. It is declared in nms_vault_protocol.h
// because the window switches on it too.
static void NotifyVaultModelChanged(NmsVaultChange why) {
    // OPEN is the one verb allowed to create the window. ADD/CLEAR are only ever
    // forwarded when an instance already exists, so they can never bring the UI
    // into existence on their own.
    if (why == NMS_VAULT_CHANGED_OPEN) {
        NmsVaultWnd::OnVaultChanged(why);
        return;
    }
    if (NmsVaultWnd::GetInstance()) {
        NmsVaultWnd::OnVaultChanged(why);
    }
}

// ---------------------------------------------------------------------------
// Chat interception
// ---------------------------------------------------------------------------
//
// Called from MQ2ChatHook.cpp for every rendered chat line. Deliberately does
// NOT consume: the hook's Filtered flag is left untouched, so the client still
// prints the line and MQ2's own filter chain still runs. The emitter notes that
// this transport is shared with real NPCs, and suppressing arbitrary NPC speech
// would be a behaviour change for unrelated content.
//
//
// Nothing is written unless the payload is a well-formed VAULTDATA command, and
// each command is applied in full or not at all -- see HandleAdd.
//
// ---------------------------------------------------------------------------
// RECOGNITION: anchored to the rendered NPC-speech body, never a bare substring
// ---------------------------------------------------------------------------
//
// The original rule was strstr(line, "VAULTDATA"), which accepted the token
// ANYWHERE in any chat line -- so a player typing "look at my VAULTDATA notes"
// could write arbitrary squares into their own vault cache. That is now rejected
// structurally.
//
// WHAT METADATA THIS HOOK HAS. CChatHook::Detour receives exactly
//     (PCHAR szMsg, DWORD dwColor, bool EqLog, bool dopercentsubst)
// from CEverQuest::dsp_chat(const char*, int, bool, bool) (EQClasses.cpp:5806).
// There is NO sender identity, NO channel number, NO spawn id and NO message
// class on this path -- only the fully rendered string and a colour. The
// discriminator below is therefore structural, not authentic.
//
// RENDERED FORM. The server sends GENERIC_SAY (string_ids.h:1032), documented as
// literally "%1 says '%2'" -- speaker, " says '", payload, "'". The player echo
// of your own speech renders with a COMMA, "You say, '...'", which is the form
// the pre-existing #autoskill filter at MQ2ChatHook.cpp:40 keys on. So:
//
//   NPC speech   <speaker> says 'VAULTDATA|...'      <- no comma
//   player echo  You say, 'VAULTDATA|...'           <- comma
//
// The two anchors are distinct, so anchoring on " says '" rejects your own
// speech without needing a secret or any protocol change.
//
// LIMITATION, STATED PLAINLY: this is structural, not authentic. dwColor is a
// client-side USERCOLOR_* value (EQData.h:183+) and the vault NPC may share one
// with ordinary speech; the server's Chat::NPCQuestSay (10) is NOT observable at
// this hook. There is no way to authenticate the speaker from dsp_chat without
// changing the server, which is out of scope. The practical exposure is
// therefore: only a line that already renders as NPC speech can reach the
// parser, which is exactly the shape a legitimate vault broadcast has.

void NmsVaultProtocol::OnChatText(const char* line) {
    if (!line || !s_instance) return;

    // Anchor on the speech introducer, never on the payload token. " says '" is
    // the GENERIC_SAY form; the player-echo form is "You say, '" (comma) and does
    // not contain it, so ordinary player chat cannot reach the parser.
    static const char kSpeechAnchor[] = " says '";
    const char* anchor = strstr(line, kSpeechAnchor);
    if (!anchor) return;

    // The payload begins IMMEDIATELY after the opening quote. Everything before it
    // is the speaker name and framing, which is never part of the protocol.
    const char* start = anchor + (sizeof(kSpeechAnchor) - 1);

    // The body ends at the closing speech quote. A payload cannot contain one:
    // VaultCleanName strips '|', ':' and '~' from item names, and the client
    // renders the body between exactly one pair of quotes.
    const char* end = strchr(start, '\'');
    if (!end || end <= start) return;

    // Isolate the body FIRST, then parse it strictly. This is what allows field 16
    // to be parsed as a plain number: the speech framing's closing quote is
    // consumed here and never reaches the field parser as trailing junk.
    s_instance->HandlePayload(start, (size_t)(end - start));
}

// ---------------------------------------------------------------------------
// Payload dispatch
// ---------------------------------------------------------------------------

bool NmsVaultProtocol::HandlePayload(const char* payload, size_t len) {
    if (!payload || len == 0) return false;

    // Split on '|' into at most 17 pieces. The bound matters: an attacker (or a
    // malformed line) must not make us allocate an unbounded vector.
    const size_t NMS_VAULT_MAX_FIELDS = 17;
    std::vector<std::string> fields;
    fields.reserve(NMS_VAULT_MAX_FIELDS);

    const char* cur = payload;
    const char* end = payload + len;
    while (cur <= end && fields.size() < NMS_VAULT_MAX_FIELDS) {
        const char* bar = (const char*)memchr(cur, '|', (size_t)(end - cur));
        const char* stop = bar ? bar : end;
        fields.emplace_back(cur, (size_t)(stop - cur));
        if (!bar) break;
        cur = bar + 1;
    }

    if (fields.size() < 2) return false;

    if (fields[0] == "VAULTDATA") {
        if (fields[1] == "CLEAR") {
            HandleClear();
            NotifyVaultModelChanged(NMS_VAULT_CHANGED_CLEAR);
            return true;
        }
        if (fields[1] == "OPEN") {
            // Rejected OPEN leaves the model untouched, so do NOT notify: the UI
            // must not react to a command that was refused.
            if (HandleOpen(fields)) {
                NotifyVaultModelChanged(NMS_VAULT_CHANGED_OPEN);
            }
            return true;
        }
        if (fields[1] == "ADD") {
            if (HandleAdd(fields)) {
                NotifyVaultModelChanged(NMS_VAULT_CHANGED_ADD);
            }
            return true;
        }
    }
    return false;
}

void NmsVaultProtocol::HandleClear() {
    // CLEAR only empties the contents. The open flag and page are protocol state
    // the server sets with OPEN, and the emitter always sends OPEN right after
    // CLEAR in the same burst, so clearing them too would only add a flicker.
    for (int i = 1; i <= NMS_VAULT_SLOT_COUNT; ++i) {
        m_slots[i] = NmsVaultSlot();
    }
}

// Returns true when the model was actually changed, so the caller (and through it
// the Patch 8 window) only reacts to an accepted command.
bool NmsVaultProtocol::HandleOpen(const std::vector<std::string>& fields) {
    // OPEN|<page> -- REJECT, never clamp.
    //
    // The previous behaviour ran atoi() and then forced anything out of range back
    // to page 1, which silently turned a malformed or hostile "OPEN|999" into a
    // valid page change. That is exactly the kind of quiet reinterpretation that
    // makes a protocol bug invisible.
    //
    // The page field is required here. The emitter only ever produces
    //     "VAULTDATA|OPEN|$page"        (NMS_vault_utils.pl :: VaultRender, line 903)
    // with $page already clamped to 1-9 by every caller, so an OPEN without a page
    // is not a form the server sends.
    //
    // ParseUintStrict requires the whole token to be digits, so a missing page, an
    // empty field, junk such as "3abc", zero, a negative or anything above 9 is
    // rejected -- and rejection means the open flag and page are left exactly as
    // they were, not reset.
    if (fields.size() < 3 || fields[2].empty()) {
        SimpleLog("NmsVault: OPEN rejected, no page field");
        return false;
    }

    uint32_t page = 0;
    if (!ParseUintStrict(fields[2], &page)) {
        SimpleLog("NmsVault: OPEN rejected, page not a plain number");
        return false;
    }
    if (page < (uint32_t)NMS_VAULT_PAGE_MIN || page > (uint32_t)NMS_VAULT_PAGE_MAX) {
        SimpleLog("NmsVault: OPEN rejected, page %u out of range %d..%d", page,
                  NMS_VAULT_PAGE_MIN, NMS_VAULT_PAGE_MAX);
        return false;
    }

    m_open = true;
    m_open_page = (int)page;
    return true;
}

// ---------------------------------------------------------------------------
// ADD
// ---------------------------------------------------------------------------

// Returns true when the slot was actually written, so the caller only refreshes
// the UI for an accepted command. Every rejection path returns false and leaves
// the model byte-for-byte unchanged.
bool NmsVaultProtocol::HandleAdd(const std::vector<std::string>& fields) {
    // Minimum viable ADD is the original 6-field form the DLL parser accepted
    // (page, slot, item_id, qty, icon, name) plus the VAULTDATA|ADD prefix.
    // Anything shorter is rejected, matching the legacy arity check.
    if (fields.size() < 8) {
        SimpleLog("NmsVault: ADD rejected, too few fields (%u)", (unsigned)fields.size());
        return false;
    }

    // ---- parse EVERYTHING into a local first -----------------------------
    // Nothing touches m_slots until every field has validated. That is what
    // makes a malformed ADD unable to half-populate a slot: a truncated or
    // corrupt line leaves the previous state of that square untouched.
    uint32_t page_raw = 0, slot = 0, item_id = 0, qty = 0, icon = 0;
    if (!ParseUintStrict(fields[2], &page_raw) ||
        !ParseUintStrict(fields[3], &slot)     ||
        !ParseUintStrict(fields[4], &item_id)  ||
        !ParseUintStrict(fields[5], &qty)      ||
        !ParseUintStrict(fields[6], &icon)) {
        SimpleLog("NmsVault: ADD rejected, bad numeric header");
        return false;
    }

    if (page_raw < (uint32_t)NMS_VAULT_PAGE_MIN || page_raw > (uint32_t)NMS_VAULT_PAGE_MAX) {
        SimpleLog("NmsVault: ADD rejected, page %u out of range", page_raw);
        return false;
    }

    const int page = (int)page_raw;

    // Global slot must land inside that page's own window, otherwise the square
    // would be invisible: the emitter's own comment records that sending slot
    // 1-5 on every page is precisely what made "only page 1 renders".
    int first = 0, last = 0;
    if (!PageSlotRange(page, &first, &last) || (int)slot < first || (int)slot > last) {
        SimpleLog("NmsVault: ADD rejected, slot %u not in page %d window %d..%d",
                  slot, page, first, last);
        return false;
    }

    if (item_id == 0) {
        SimpleLog("NmsVault: ADD rejected, item id 0");
        return false;
    }

    const std::string name = fields[7];      // delimiter-free: see header
    const std::string contents = (fields.size() > 8) ? fields[8] : std::string();

    uint32_t augments[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 6; ++i) {
        const size_t idx = 9 + (size_t)i;
        if (fields.size() > idx && !fields[idx].empty()) {
            // An augment slot that is absent or empty is 0, not an error: the
            // emitter writes `$r->{$_} || 0`.
            if (!ParseUintStrict(fields[idx], &augments[i])) {
                SimpleLog("NmsVault: ADD rejected, bad augment field %d", i + 1);
                return false;
            }
        }
    }

    uint32_t unread = 0;
    if (fields.size() > 15 && !fields[15].empty()) {
        if (!ParseUintStrict(fields[15], &unread)) {
            SimpleLog("NmsVault: ADD rejected, bad unread flag");
            return false;
        }
    }

    uint32_t bag_cap = 0;
    if (fields.size() > 16 && !fields[16].empty()) {
        // STRICT, like every other numeric field.
        //
        // This used to be the one lenient parse, on the grounds that NPC speech
        // appends a closing quote to the payload and it would land on this field.
        // That is no longer necessary: OnChatText now isolates the speech body
        // between the opening and closing quotes BEFORE calling HandlePayload, so
        // the framing quote never becomes part of field 16. The lenient rule is
        // therefore removed rather than narrowed -- there is no transport artifact
        // left for it to tolerate, and general permission for arbitrary trailing
        // junk on the last field was never justified.
        if (!ParseUintStrict(fields[16], &bag_cap)) {
            SimpleLog("NmsVault: ADD rejected, bad bag capacity");
            return false;
        }
    }

    // Nested bag items. A malformed nested entry rejects the whole ADD rather
    // than storing a container whose contents cannot be trusted.
    std::vector<NmsVaultBagItem> bag_items;
    if (!contents.empty() && !ParseBagContents(contents, &bag_items)) {
        SimpleLog("NmsVault: ADD rejected, malformed bag contents");
        return false;
    }

    // ---- commit ------------------------------------------------------------
    NmsVaultSlot& dst = m_slots[(int)slot];
    dst.valid    = true;
    dst.page     = page;
    dst.item_id  = item_id;
    dst.qty      = qty;
    dst.icon     = icon;
    dst.name     = name;
    dst.contents = contents;
    for (int i = 0; i < 6; ++i) dst.augments[i] = augments[i];
    dst.unread  = unread;
    dst.bag_cap = bag_cap;
    dst.bag_items.swap(bag_items);
    return true;
}

// "<item_id>:<charges>:<icon>:<bag_slot>:<name>" joined by '~'.
// Names are delimiter-stripped by VaultCleanName, so ':' and '~' are safe.
bool NmsVaultProtocol::ParseBagContents(const std::string& text,
                                        std::vector<NmsVaultBagItem>* out) const {
    if (!out) return false;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t tilde = text.find('~', pos);
        std::string part = (tilde == std::string::npos)
                               ? text.substr(pos)
                               : text.substr(pos, tilde - pos);

        if (!part.empty()) {
            // Four colons: id, charges, icon, bag_slot, name.
            std::vector<std::string> f;
            f.reserve(6);
            size_t cpos = 0;
            while (cpos <= part.size()) {
                size_t colon = part.find(':', cpos);
                f.emplace_back(colon == std::string::npos ? part.substr(cpos)
                                                          : part.substr(cpos, colon - cpos));
                if (colon == std::string::npos) break;
                cpos = colon + 1;
            }
            if (f.size() < 5) return false;

            NmsVaultBagItem item;
            if (!ParseUintStrict(f[0], &item.item_id))  return false;
            if (!ParseUintStrict(f[1], &item.charges))  return false;
            if (!ParseUintStrict(f[2], &item.icon))     return false;
            if (!ParseUintStrict(f[3], &item.bag_slot)) return false;
            if (item.item_id == 0) return false;
            item.name = f[4];
            out->push_back(item);
        }

        if (tilde == std::string::npos) break;
        pos = tilde + 1;
        // Bound the loop: a pathological payload must not spin.
        if (out->size() > 256) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Model accessors
// ---------------------------------------------------------------------------

bool NmsVaultProtocol::PageSlotRange(int page, int* first, int* last) const {
    if (!first || !last) return false;
    if (page < NMS_VAULT_PAGE_MIN || page > NMS_VAULT_PAGE_MAX) return false;
    *first = ((page - 1) * 10) + 1;
    *last  = (page == NMS_VAULT_PAGE_SPECIAL) ? (*first + 2) : (page * 10);
    return true;
}

int NmsVaultProtocol::SlotCount() const {
    int n = 0;
    for (int i = NMS_VAULT_FIRST_SLOT; i <= NMS_VAULT_LAST_SLOT; ++i) {
        if (m_slots[i].valid) ++n;
    }
    return n;
}

const NmsVaultSlot& NmsVaultProtocol::Slot(int global_slot) const {
    if (global_slot < NMS_VAULT_FIRST_SLOT || global_slot > NMS_VAULT_LAST_SLOT) {
        return m_empty_slot;
    }
    return m_slots[global_slot];
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Every cached square belongs to one character. Leaving ingame (zone change,
// logout, character select) must not carry another character's vault into the
// next session, so the whole model is dropped -- contents, open flag and page.
// This is the same hook and the same rule MultiPet uses for its pet table.
void NmsVaultProtocol::OnSetGameState(int gameState) {
    if (gameState != 5) {  // 5 == GAMESTATE_INGAME
        ClearAll();
    }
}
