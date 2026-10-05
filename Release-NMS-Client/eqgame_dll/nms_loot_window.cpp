// NMS-LOCAL: native pending-loot and looter-roster window (Patch 9).
//
// A view over Patch 7's models. See nms_loot_window.h for the contract: this file
// never parses 0x140A/0x140D, never caches offers or names, and never builds a
// 0x140B packet. Actions go through NmsLootProtocol::SendReply().

#include "MQ2Main.h"
#include "nms_loot_window.h"
#include "core_log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

extern void AddXMLFile(const char* filename);

// Forward declared because Initialize() registers it below while its definition
// sits further down with the other helpers. Signature per multi_pet.cpp:144.
static VOID CmdToggleLootWnd(PSPAWNINFO pChar, PCHAR szLine);

NmsLootWnd* NmsLootWnd::s_instance = nullptr;

NmsLootWnd* NmsLootWnd::GetInstance() { return s_instance; }

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Action buttons, in the order the XML declares them. The value is the server's
// action code from nms_loot_protocol.h, which mirrors nms_lootoffer.cpp exactly --
// no renumbering, no local translation table of our own.
struct ActionBinding {
    const char* child;
    uint8_t     action;
};

static const ActionBinding kActions[] = {
    { "NMSLootKeep",     NMS_LOOT_KEEP    },
    { "NMSLootSell",     NMS_LOOT_SELL    },
    { "NMSLootTribute",  NMS_LOOT_TRIBUTE },
    { "NMSLootBank",     NMS_LOOT_BANK    },
    { "NMSLootVault",    NMS_LOOT_VAULT   },
    { "NMSLootDestroy",  NMS_LOOT_DESTROY },
    { "NMSLootPass",     NMS_LOOT_PASS    }
};

static const size_t kActionCount = sizeof(kActions) / sizeof(kActions[0]);

void NmsLootWnd::Initialize() {
    // Same call NmsVaultWnd::Initialize() makes; the template is injected rather
    // than added to any EQUI.xml, so no existing UI resource is touched.
    AddXMLFile("NMS_LootWnd.xml");

    // /nmsloot reopens the decision queue after the player closes it. BFE's DLL
    // registered exactly this slash command (recorded in
    // Release-NMS-Quests/global/global_player.pl:936), and without it a closed
    // window would have no way back except the next offer.
    //
    // Registered HERE rather than in MQ2CommandAPI.cpp: that file owns the generic
    // command table and interception machinery, while NMS commands register
    // themselves in their own module -- the established precedent is MultiPet
    // (multi_pet.cpp:170-172 registering /pets, /petcycle, /petdebug from
    // Initialize). Touching the shared table for one window would risk every other
    // command for no benefit.
    AddCommand("/nmsloot", CmdToggleLootWnd);
}

void NmsLootWnd::Shutdown() {
    RemoveCommand("/nmsloot");

    if (s_instance) {
        ((CXWnd*)s_instance)->Show(false, false);
        delete s_instance;
        s_instance = nullptr;
    }
}

void NmsLootWnd::OnSetGameState(int state) {
    // Patch 7 already clears BOTH models on any transition away from ingame
    // (NmsLootProtocol::OnSetGameState, called immediately before this from
    // MQ2Pulse.cpp). The window only stops being visible and drops its transient
    // selection -- it must NOT clear the models itself, or it would be resetting
    // Patch 7 state from a UI event.
    if (state != 5) {
        if (s_instance) {
            ((CXWnd*)s_instance)->Show(false, false);
            s_instance->m_selected_row = -1;
            s_instance->m_selected_roster = -1;
        }
    }
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

NmsLootWnd::NmsLootWnd() : CCustomWnd("NMSLootWnd") {
    m_pPendingList = (CListWnd*)GetChildItem("NMSLootPendingList");
    m_pRosterList  = (CListWnd*)GetChildItem("NMSLootRosterList");

    m_pKeepButton    = (CButtonWnd*)GetChildItem(kActions[0].child);
    m_pSellButton    = (CButtonWnd*)GetChildItem(kActions[1].child);
    m_pTributeButton = (CButtonWnd*)GetChildItem(kActions[2].child);
    m_pBankButton    = (CButtonWnd*)GetChildItem(kActions[3].child);
    m_pVaultButton   = (CButtonWnd*)GetChildItem(kActions[4].child);
    m_pDestroyButton = (CButtonWnd*)GetChildItem(kActions[5].child);
    m_pPassButton    = (CButtonWnd*)GetChildItem(kActions[6].child);
    m_pCloseButton   = (CButtonWnd*)GetChildItem("NMSLootClose");

    if (!m_pPendingList) WriteChatf("[Loot] Missing NMSLootPendingList");
    if (!m_pRosterList)  WriteChatf("[Loot] Missing NMSLootRosterList");

    SetWndNotification(NmsLootWnd);

    m_selected_row    = -1;
    m_selected_roster = -1;
    m_pending_rows    = 0;
    m_roster_rows     = 0;
}

NmsLootWnd::~NmsLootWnd() {}

// ---------------------------------------------------------------------------
// /nmsloot
// ---------------------------------------------------------------------------

// Command handler signature per multi_pet.cpp:144 -- (PSPAWNINFO, PCHAR).
static VOID CmdToggleLootWnd(PSPAWNINFO /*pChar*/, PCHAR /*szLine*/) {
    NmsLootWnd::Toggle();
}
void NmsLootWnd::Toggle() {
    NmsLootProtocol* proto = NmsLootProtocol::GetInstance();
    if (!proto) return;

    // Already up: hide it. Closing the UI is NOT a decision -- the offers stay in
    // the authoritative model and can be reopened with /nmsloot.
    if (s_instance && ((CXWnd*)s_instance)->IsReallyVisible()) {
        ((CXWnd*)s_instance)->Show(false, false);
        return;
    }

    if (proto->Pending().empty()) {
        WriteChatf("[Loot] Nothing pending a decision.");
        return;
    }

    if (!pSidlMgr || !pSidlMgr->FindScreenPieceTemplate("NMSLootWnd")) {
        SimpleLog("NmsLoot: template 'NMSLootWnd' not found, cannot show");
        WriteChatf("[Loot] Window template unavailable - UI may need reloading.");
        return;
    }

    // Lazy singleton, same guards as the vault window.
    if (!s_instance) {
        s_instance = new NmsLootWnd();
    }
    if (!s_instance) return;

    s_instance->RefreshPending();
    s_instance->RefreshRoster();
    ((CXWnd*)s_instance)->Show(true, true);
}

// ---------------------------------------------------------------------------
// Model notification
// ---------------------------------------------------------------------------

void NmsLootWnd::OnLootModelChanged(Change why) {
    switch (why) {

    case CHANGE_PENDING_NEW: {
        // The open trigger. Evidence: BFE docs/NMSLOOT_PROTOCOL.md:534 records that
        // an arriving offer is announced and the item lands in the decision queue, so
        // a new non-empty packet is exactly when the player needs this window.
        NmsLootProtocol* proto = NmsLootProtocol::GetInstance();
        if (!proto || proto->Pending().empty()) return;

        if (!pSidlMgr || !pSidlMgr->FindScreenPieceTemplate("NMSLootWnd")) {
            // Template unavailable: the offers stay in the model and /nmsloot will
            // retry. No poll loop -- see the note in NmsVaultWnd::ShowPage.
            SimpleLog("NmsLoot: template unavailable, offers kept in model");
            return;
        }
        if (!s_instance) {
            s_instance = new NmsLootWnd();
        }
        if (!s_instance) return;

        // A brand new queue invalidates whatever was selected.
        s_instance->m_selected_row = -1;
        s_instance->RefreshPending();
        s_instance->RefreshRoster();
        s_instance->SetStatus("New loot to review.");
        ((CXWnd*)s_instance)->Show(true, true);
        return;
    }

    case CHANGE_ROSTER:
        // Supporting data only; never opens anything.
        if (s_instance) {
            s_instance->RefreshRoster();
        }
        return;

    case CHANGE_PENDING_CLEAR:
    case CHANGE_LOCAL_REPLY:
        // Refresh-only. Reaching here already implies an instance, because the
        // protocol layer does not forward these at all when none exists.
        if (s_instance) {
            s_instance->RefreshPending();
            s_instance->RefreshRoster();
        }
        return;

    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// Refresh
// ---------------------------------------------------------------------------

void NmsLootWnd::RefreshPending() {
    NmsLootProtocol* proto = NmsLootProtocol::GetInstance();
    if (!proto || !m_pPendingList) return;

    m_pPendingList->DeleteAll();
    m_pending_rows = 0;

    const std::vector<NmsLootOfferRow>& rows = proto->Pending();
    if (rows.empty()) {
        // Deterministic empty state: no rows, and every action disabled so a stale
        // selection cannot act on a row that no longer exists.
        m_selected_row = -1;
        RefreshActionButtons();
        return;
    }

    char buf[512];

    for (size_t i = 0; i < rows.size(); ++i) {
        const NmsLootOfferRow& r = rows[i];

        std::string line = r.item_name.empty()
                         ? ("item " + std::to_string(r.item_ref))
                         : r.item_name;

        if (!r.from_player.empty()) {
            line += "  from " + r.from_player;
        }

        // Copies/charges are not in the reply-visible fields, so they are NOT
        // invented here -- the model does not carry them for pending rows. What the
        // protocol does carry is the sell value and the tribute value, which are
        // the numbers a player actually decides on.
        if (r.sell_value > 0) {
            line += "  sells " + std::to_string(r.sell_value) + "cp";
        }
        if (r.tribute_value > 0) {
            line += "  tribute " + std::to_string(r.tribute_value);
        }
        if (r.bonus_flag) {
            line += "  [BONUS]";
        }

        if (r.submitted) {
            line += "   (sent)";
        }

        snprintf(buf, sizeof(buf), "%s", line.c_str());

        // Tooltip is plain text built only from model fields. A native item tooltip
        // would need a real _CONTENTS/_ITEMINFO, which a pending offer does not
        // have and must not be faked for -- the same limitation the vault window has.
        char tip[384];
        snprintf(tip, sizeof(tip),
                 "offer %u\n%s\nitem %u   icon %u\nfrom: %s\nsell %u copper   tribute %u%s",
                 (unsigned)r.id,
                 r.item_name.empty() ? "(no name)" : r.item_name.c_str(),
                 (unsigned)r.item_ref, (unsigned)r.icon,
                 r.from_player.empty() ? "(unknown)" : r.from_player.c_str(),
                 (unsigned)r.sell_value, (unsigned)r.tribute_value,
                 r.submitted ? "\n(decision already sent)" : "");

        // Data is the offer id, so a click is validated against the authoritative
        // model rather than trusted from a row index.
        const int row = m_pPendingList->AddString(buf, 0xFFFFFFFF,
                                                   (uint32_t)r.id, 0, tip);
        if (row < 0) continue;

        // A submitted row cannot be acted on again before the server refreshes.
        m_pPendingList->EnableLine(row, !r.submitted);
        m_pending_rows++;
    }

    // Selection may point past the end after a shrink.
    if (m_selected_row >= m_pending_rows) {
        m_selected_row = -1;
    }
    RefreshActionButtons();
}

void NmsLootWnd::RefreshRoster() {
    NmsLootProtocol* proto = NmsLootProtocol::GetInstance();
    if (!proto || !m_pRosterList) return;

    // Remember what was selected by NAME, not index: the roster is replaced
    // wholesale on every 0x140D, so an index would silently point at a different
    // player after a refresh.
    std::string prev;
    if (m_selected_roster >= 0 && m_selected_roster < (int)proto->Roster().size()) {
        prev = proto->Roster()[(size_t)m_selected_roster];
    }

    m_pRosterList->DeleteAll();
    m_roster_rows = 0;

    const std::vector<std::string>& names = proto->Roster();
    m_selected_roster = -1;

    char tip[128];
    for (size_t i = 0; i < names.size(); ++i) {
        snprintf(tip, sizeof(tip), "Pass the selected item to %s", names[i].c_str());
        const int row = m_pRosterList->AddString(names[i].c_str(), 0xFFFFFFFF,
                                                  (uint32_t)i, 0, tip);
        if (row < 0) continue;
        // Restore the selection only if that name is still present. If the roster
        // changed and the name is gone, the selection stays cleared -- Pass must
        // never fall back to some other player.
        if (!prev.empty() && names[i] == prev) {
            m_selected_roster = row;
        }
        m_roster_rows++;
    }

    RefreshActionButtons();
}

void NmsLootWnd::RefreshActionButtons() {
    // An action needs a live, unspent selection. Pass additionally needs a recipient
    // because the server routes action 9 by NAME: it looks the name up with
    // entity_list.GetClientByName() and, on a miss, returns the item to the sender
    // instead of passing it (nms_lootoffer.cpp :: PassNMSLootOffer). Sending a
    // guess would therefore bounce the item back rather than pass it.
    uint32_t offer_id = 0;
    const bool can_act = ResolveSelection(&offer_id);
    const bool can_pass = can_act && m_selected_roster >= 0
                          && m_selected_roster < m_roster_rows;

    // CButtonWnd is a CSidlScreenWnd, not a CXWnd, so Show() needs the cast -- the
    // same pattern the vault window uses.
    if (m_pKeepButton)    ((CXWnd*)m_pKeepButton)->Show(can_act, true);
    if (m_pSellButton)    ((CXWnd*)m_pSellButton)->Show(can_act, true);
    if (m_pTributeButton) ((CXWnd*)m_pTributeButton)->Show(can_act, true);
    if (m_pBankButton)    ((CXWnd*)m_pBankButton)->Show(can_act, true);
    if (m_pVaultButton)   ((CXWnd*)m_pVaultButton)->Show(can_act, true);
    if (m_pDestroyButton) ((CXWnd*)m_pDestroyButton)->Show(can_act, true);
    if (m_pPassButton)    ((CXWnd*)m_pPassButton)->Show(can_pass, true);
}

void NmsLootWnd::SetStatus(const char* text) {
    // No runtime label-text setter exists in this client (see nms_vault_window.h),
    // so status goes to chat. Only non-empty messages print, so ordinary refreshes
    // stay silent.
    if (text && *text) {
        WriteChatf("[Loot] %s", text);
    }
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

bool NmsLootWnd::ResolveSelection(uint32_t* offer_id) const {
    if (!m_pPendingList || !offer_id) return false;

    const int row = m_pPendingList->GetCurSel();
    if (row < 0 || row >= m_pending_rows) return false;

    NmsLootProtocol* proto = NmsLootProtocol::GetInstance();
    if (!proto) return false;

    const uint32_t id = m_pPendingList->GetItemData(row);
    if (id == 0) return false;

    // The authoritative model must still hold this offer. This is what makes a
    // stale row harmless after the server replaced the set.
    const NmsLootOfferRow* found = proto->FindById(id);
    if (!found) return false;

    // Already submitted: the server may not have refreshed yet, but the decision is
    // spent. Refusing here is the local half of Patch 7's double-submit protection.
    if (found->submitted) return false;

    *offer_id = id;
    return true;
}

bool NmsLootWnd::ActionButtonFor(CXWnd* pWnd, uint8_t* action_out) const {
    for (size_t i = 0; i < kActionCount; ++i) {
        CButtonWnd* b = NULL;
        switch (i) {
        case 0: b = m_pKeepButton;    break;
        case 1: b = m_pSellButton;    break;
        case 2: b = m_pTributeButton; break;
        case 3: b = m_pBankButton;    break;
        case 4: b = m_pVaultButton;   break;
        case 5: b = m_pDestroyButton; break;
        default: b = m_pPassButton;   break;
        }
        if (b && pWnd == (CXWnd*)b) {
            if (action_out) *action_out = kActions[i].action;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

bool NmsLootWnd::SubmitAction(uint8_t action, const char* target) {
    uint32_t offer_id = 0;
    if (!ResolveSelection(&offer_id)) {
        // Nothing selected, the offer is gone, or it was already answered. The
        // refresh makes that visible instead of leaving a live-looking button.
        RefreshPending();
        SetStatus("Nothing left to answer on that row.");
        return false;
    }

    // Pass routes by name, so the recipient must come from the authoritative
    // roster -- never typed, never guessed, never defaulted to the offer's sender.
    if (action == NMS_LOOT_PASS) {
        NmsLootProtocol* proto = NmsLootProtocol::GetInstance();
        if (!proto || m_selected_roster < 0 || m_selected_roster >= (int)proto->Roster().size()) {
            SetStatus("Select who to pass to first.");
            return false;
        }
        target = proto->Roster()[(size_t)m_selected_roster].c_str();
    }

    NmsLootProtocol* proto = NmsLootProtocol::GetInstance();
    if (!proto) return false;

    // Patch 7 owns the reply: it echoes the offer fields the server cross-checks,
    // marks the row submitted BEFORE sending, and refuses a replay. This window
    // never touches NmsLootReply or SendEQMessage.
    if (!proto->SendReply(offer_id, action, target)) {
        RefreshPending();
        return false;
    }

    // SendReply() has already marked the row and notified the model layer, which
    // refreshed us -- but RefreshPending() is idempotent and guarantees the visible
    // state matches even if that notification path ever changes.
    RefreshPending();
    return true;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

int NmsLootWnd::WndNotification(CXWnd* pWnd, unsigned int Message, void* data) {
    // No titlebar-closebox or Escape branch: MQ2Main.h defines no XWM_CLOSE, and
    // Style_Closebox/Escapable are handled by CSidlScreenWnd itself. The explicit
    // NMSLootClose button covers mouse-driven closing.
    if (Message != XWM_LCLICK) {
        return CSidlScreenWnd::WndNotification(pWnd, Message, data);
    }

    if (m_pCloseButton && pWnd == (CXWnd*)m_pCloseButton) {
        // Hide only. The offers stay in Patch 7's model; closing the UI is not a
        // decision and must not discard anything.
        ((CXWnd*)this)->Show(false, false);
        SetStatus("Pending offers kept - /nmsloot reopens them.");
        return 0;
    }

    if (m_pPendingList && pWnd == (CXWnd*)m_pPendingList) {
        m_selected_row = m_pPendingList->GetCurSel();
        RefreshActionButtons();
        return 0;
    }

    if (m_pRosterList && pWnd == (CXWnd*)m_pRosterList) {
        m_selected_roster = m_pRosterList->GetCurSel();
        RefreshActionButtons();
        return 0;
    }

    uint8_t action = 0;
    if (ActionButtonFor(pWnd, &action)) {
        SubmitAction(action, NULL);
        return 0;
    }

    return CSidlScreenWnd::WndNotification(pWnd, Message, data);
}
