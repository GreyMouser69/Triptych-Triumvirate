// NMS-LOCAL: native Dimensional Pocket Vault window (Patch 8).
//
// A VIEW over the Patch 7 model. This file never parses VAULTDATA and never
// caches vault contents independently -- every refresh reads
// NmsVaultProtocol::Slot() and friends. See nms_vault_window.h.

#include "MQ2Main.h"
#include "nms_vault_window.h"
#include "core_log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

extern void AddXMLFile(const char *filename);

// The established outbound path for client-issued commands, per the plan
// section 8.3 ("Emit through the established InterpretCmd path").
//
// Uses the real client entry point directly rather than an invented wrapper:
//   CEverQuest::InterpretCmd  (EQClasses.h:1829)
//   pEverQuest                (MQ2Globals.h:354)
// Hooks.cpp:1582-1586 is the existing in-tree precedent for this exact shape
// (it emits "/say #alttoggle ..." the same way), so nothing new is introduced.

// Page-button item names, matching NMS_VaultWnd.xml.
static const char* kPageButtonNames[NMS_VAULT_PAGE_MAX] = {
    "NMSVaultPage1", "NMSVaultPage2", "NMSVaultPage3",
    "NMSVaultPage4", "NMSVaultPage5", "NMSVaultPage6",
    "NMSVaultPage7", "NMSVaultPage8", "NMSVaultPage9"
};

NmsVaultWnd* NmsVaultWnd::s_instance = nullptr;

NmsVaultWnd* NmsVaultWnd::GetInstance() { return s_instance; }

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void NmsVaultWnd::Initialize() {
    // Same call WaypointsWnd::Initialize() makes. The template is injected rather
    // than added to any EQUI.xml, so no existing UI resource is touched.
    AddXMLFile("NMS_VaultWnd.xml");
}

void NmsVaultWnd::Shutdown() {
    if (s_instance) {
        ((CXWnd*)s_instance)->Show(false, false);
        delete s_instance;
        s_instance = nullptr;
    }
}

void NmsVaultWnd::OnSetGameState(int state) {
    // Patch 7 already clears the authoritative model on any transition away from
    // ingame (NmsVaultProtocol::OnSetGameState). The window only has to stop being
    // visible -- it must NOT clear the model itself, or it would be resetting
    // Patch 7 state from a UI event.
    if (state != 5) {
        if (s_instance) {
            ((CXWnd*)s_instance)->Show(false, false);
        }
    }
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

NmsVaultWnd::NmsVaultWnd() : CCustomWnd("NMSVaultWnd") {
    m_pItemList     = (CListWnd*)  GetChildItem("NMSVaultItemList");
    m_pContentsList = (CListWnd*)  GetChildItem("NMSVaultContentsList");
    m_pPageIndicator = (CButtonWnd*)GetChildItem("NMSVaultPageIndicator");
    m_pPrevButton   = (CButtonWnd*)GetChildItem("NMSVaultPrevPage");
    m_pNextButton   = (CButtonWnd*)GetChildItem("NMSVaultNextPage");
    m_pWithdrawButton = (CButtonWnd*)GetChildItem("NMSVaultWithdraw");
    m_pCloseButton  = (CButtonWnd*)GetChildItem("NMSVaultClose");

    for (int i = 0; i < NMS_VAULT_PAGE_MAX; ++i) {
        m_pPageButtons[i] = (CButtonWnd*)GetChildItem(kPageButtonNames[i]);
    }

    if (!m_pItemList)     WriteChatf("[Vault] Missing NMSVaultItemList");
    if (!m_pContentsList) WriteChatf("[Vault] Missing NMSVaultContentsList");

    SetWndNotification(NmsVaultWnd);
    (void)m_pPageIndicator;   // bound, present for layout; see Refresh()

    // Start on page 1; ShowPage()/OPEN overrides this.
    m_page = NMS_VAULT_PAGE_MIN;
    m_selected_slot = 0;
    m_rows = 0;
    m_contents_rows = 0;
}

NmsVaultWnd::~NmsVaultWnd() {
    // Detach the vtable before the derived state is gone: CCustomWnd's dtor does
    // this itself (MQ2Internal.h:395), but being explicit costs nothing and keeps
    // the ordering obvious.
}

// ---------------------------------------------------------------------------
// Showing
// ---------------------------------------------------------------------------

void NmsVaultWnd::ShowPage(int page) {
    // Clamp to the legal page range. Page 9 keeps its own 3-slot restriction
    // inside Refresh(); this is only the outer bound.
    if (page < NMS_VAULT_PAGE_MIN) page = NMS_VAULT_PAGE_MIN;
    if (page > NMS_VAULT_PAGE_MAX) page = NMS_VAULT_PAGE_MAX;

    // Lazy construction, exactly like WaypointsWnd::OnIncomingPacket does it.
    if (!pSidlMgr || !pSidlMgr->FindScreenPieceTemplate("NMSVaultWnd")) {
        // Template unavailable: skin missing, or AddXMLFile has not run for the
        // current UI yet. Reported to the player rather than swallowed, matching
        // what WaypointsWnd does for its own missing template
        // (waypoint_window.cpp:91), because a vault window that silently never
        // appears is indistinguishable from a broken vault.
        //
        // No retry is attempted here. The Patch 7 model keeps the page and its
        // slots intact, so the data is not lost, and the next accepted OPEN will
        // try again -- which covers the realistic case, a /loadskin in progress,
        // since CleanUI destroys the instance and the template returns with the
        // reloaded UI. Inventing a poll loop for this would be new machinery.
        SimpleLog("NmsVault: template 'NMSVaultWnd' not found, cannot show");
        WriteChatf("[Vault] Window template unavailable - UI may need reloading.");
        return;
    }

    if (!s_instance) {
        s_instance = new NmsVaultWnd();
    }
    if (!s_instance) return;

    s_instance->m_page = page;
    s_instance->m_selected_slot = 0;
    s_instance->Refresh();
    ((CXWnd*)s_instance)->Show(true, true);
}

void NmsVaultWnd::OnVaultChanged(NmsVaultChange why) {
    NmsVaultProtocol* proto = NmsVaultProtocol::GetInstance();
    if (!proto) return;

    switch (why) {

    case NMS_VAULT_CHANGED_OPEN:
        // The only path that creates and shows. A first OPEN of the session has no
        // instance yet, and must still bring the window up by itself -- the player
        // must not have to ask twice.
        ShowPage(proto->OpenPage());
        return;

    case NMS_VAULT_CHANGED_ADD:
    case NMS_VAULT_CHANGED_CLEAR:
        // Refresh only. The protocol layer does not forward these at all when no
        // window exists, so reaching here already implies an instance.
        if (s_instance) {
            s_instance->Refresh();
        }
        return;

    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// Refresh
// ---------------------------------------------------------------------------

void NmsVaultWnd::Refresh() {
    NmsVaultProtocol* proto = NmsVaultProtocol::GetInstance();
    if (!proto) return;

    if (!m_pItemList || !m_pContentsList) {
        // Controls missing means the template did not load or is a different
        // skin. Nothing can be displayed; do not touch half-bound state.
        return;
    }

    // The window is never showing a page the model did not select unless the
    // player navigated locally. Navigation updates m_page; the model is
    // untouched until the server replies with OPEN for the new page.
    int first = 0, last = 0;
    const bool have_range = proto->PageSlotRange(m_page, &first, &last);

    // Drop the old rows with CListWnd::DeleteAll, which is the established call
    // for this in the existing window (waypoint_window.cpp:207). An earlier
    // version of this file looped RemoveString() backwards because it looked like
    // DeleteAll might not exist -- it does (EQClasses.h:2827), and using it removes
    // the row counter from the clear path entirely.
    if (m_pItemList)     m_pItemList->DeleteAll();
    if (m_pContentsList) m_pContentsList->DeleteAll();
    m_rows = 0;
    m_contents_rows = 0;

    if (!have_range) {
        SetStatus("No page.");
        return;
    }

    char buf[512];

    for (int slot = first; slot <= last; ++slot) {
        const NmsVaultSlot& s = proto->Slot(slot);

        // Row text. Everything shown comes from the model; nothing is invented.
        std::string line;
        if (s.valid) {
            line = s.name.empty() ? ("item " + std::to_string(s.item_id)) : s.name;

            // Quantity only when it is meaningful, so a plain 1-charge item does
            // not read "x1" noise on every row.
            if (s.qty > 1) {
                line += "  x" + std::to_string(s.qty);
            }

            // Container marker: the server sends bag capacity and only draws a
            // square as a container when it is > 0.
            if (s.bag_cap > 0) {
                char cbuf[64];
                snprintf(cbuf, sizeof(cbuf), "  [bag %u/%u]",
                         (unsigned)s.bag_items.size(), (unsigned)s.bag_cap);
                line += cbuf;
            }

            // Augments, only when the server actually sent one.
            std::string augs;
            for (int a = 0; a < 6; ++a) {
                if (s.augments[a]) {
                    if (!augs.empty()) augs += ",";
                    augs += std::to_string(s.augments[a]);
                }
            }
            if (!augs.empty()) {
                line += "  [aug " + augs + "]";
            }
        } else {
            // Empty slot, deterministic.
            snprintf(buf, sizeof(buf), "(empty)");
            line = buf;
        }

        snprintf(buf, sizeof(buf), "%3d  %s", slot, line.c_str());

        // Tooltip: plain text describing the model, NOT a native item tooltip.
        // See the header note -- a native tooltip needs a real _CONTENTS /
        // _ITEMINFO, which a vault row does not have and must not be faked for.
        char tip[512];
        if (s.valid) {
            snprintf(tip, sizeof(tip),
                     "Slot %d\n%s\nid %u  x%u  icon %u\npage %d%s",
                     slot, s.name.empty() ? "(no name)" : s.name.c_str(),
                     (unsigned)s.item_id, (unsigned)s.qty, (unsigned)s.icon,
                     s.page, s.bag_cap > 0 ? "\n(container)" : "");
        } else {
            snprintf(tip, sizeof(tip), "Slot %d\nempty", slot);
        }

        // Data carries the global slot number so selection can be validated
        // against the model rather than trusted from the row index.
        const int row = m_pItemList->AddString(buf, 0xFFFFFFFF,
                                               (uint32_t)slot, 0, tip);
        if (row < 0) continue;   // defensive: a refused row must not desync m_rows

        // Page 9 owns only three real slots. Its row loop runs to `last` (81..83),
        // so there are no phantom 84..90 cells to disable here -- the window
        // simply never creates them. The guard below is therefore belt-and-braces
        // for a model that somehow reports a slot past the page's window.
        if (slot > last) {
            m_pItemList->EnableLine(row, false);
        }
        m_rows++;
    }

    // Page indicator always reflects the page actually being displayed. It is a
    // button, not a Label, because this client exposes no runtime label-text
    // setter; see the control note in nms_vault_window.h.
    // Report the page being shown once per refresh, since the on-screen page
    // indicator cannot be rewritten at runtime (see the control note in the
    // header). The row text carries the authoritative slot numbers regardless.
    {
        const char* kind = "";
        if (m_page >= NMS_VAULT_PAGE_CLICKY_FIRST && m_page <= NMS_VAULT_PAGE_CLICKY_LAST) {
            kind = " (clicky)";
        } else if (m_page == NMS_VAULT_PAGE_SPECIAL) {
            kind = " (special)";
        }
        snprintf(buf, sizeof(buf), "Page %d of %d%s  slots %d-%d",
                 m_page, NMS_VAULT_PAGE_MAX, kind, first, last);
        SetStatus(buf);
    }

    RefreshPageButtons();
    RefreshContents();
    SetStatus("");

    // Withdraw is offered only while a real selection exists. After CLEAR the
    // previously selected slot is gone, so the button is hidden rather than left
    // enabled over nothing. Cast for the same reason as the page buttons: CButtonWnd
    // is not a CXWnd.
    if (m_pWithdrawButton) {
        const bool can_act = m_selected_slot != 0
                          && proto->Slot(m_selected_slot).valid;
        ((CXWnd*)m_pWithdrawButton)->Show(can_act, true);
    }
}

void NmsVaultWnd::RefreshContents() {
    if (!m_pContentsList) return;

    NmsVaultProtocol* proto = NmsVaultProtocol::GetInstance();
    if (!proto) return;

    if (!m_selected_slot) return;

    const NmsVaultSlot& s = proto->Slot(m_selected_slot);
    if (!s.valid) return;

    // Bag contents come from the nested "contents" field Patch 7 already parsed
    // into bag_items. Not re-parsed here.
    for (size_t i = 0; i < s.bag_items.size(); ++i) {
        const NmsVaultBagItem& b = s.bag_items[i];
        char buf[256];
        std::string nm = b.name.empty() ? ("item " + std::to_string(b.item_id)) : b.name;
        snprintf(buf, sizeof(buf), "  bag %u  %s  x%u  id %u",
                 (unsigned)b.bag_slot, nm.c_str(),
                 (unsigned)b.charges, (unsigned)b.item_id);
        char tip[256];
        snprintf(tip, sizeof(tip), "Bag slot %u\n%s\nid %u  charges %u",
                 (unsigned)b.bag_slot, nm.c_str(),
                 (unsigned)b.item_id, (unsigned)b.charges);
        if (m_pContentsList->AddString(buf, 0xFFFFFFFF,
                                       (uint32_t)b.bag_slot, 0, tip) >= 0) {
            m_contents_rows++;
        }
    }
}

void NmsVaultWnd::RefreshPageButtons() {
    for (int i = 0; i < NMS_VAULT_PAGE_MAX; ++i) {
        if (!m_pPageButtons[i]) continue;
        // CButtonWnd::SetCheck, not SetChecked -- this is the method that exists in
        // EQClasses.h. The active page's button reads as pressed so the current
        // page is visible at a glance.
        m_pPageButtons[i]->SetCheck((i + 1) == m_page);
    }
    // Prev/Next are hidden at the ends so the page can never go out of range.
    //
    // CButtonWnd derives from CSidlScreenWnd, NOT from CXWnd, so Show() is not
    // reachable on it -- the cast is the established pattern (the waypoint window
    // does the same for its own window, waypoint_window.cpp:83/104). Show() is
    // still preferred over toggling Enabled because a disabled Prev button would
    // still be visible and might read as "broken" rather than "you are at the end".
    if (m_pPrevButton) ((CXWnd*)m_pPrevButton)->Show(m_page > NMS_VAULT_PAGE_MIN, true);
    if (m_pNextButton) ((CXWnd*)m_pNextButton)->Show(m_page < NMS_VAULT_PAGE_MAX, true);
}

void NmsVaultWnd::SetStatus(const char* text) {
    // No label-text setter exists in this binding, so status goes to the chat
    // line. Only non-empty messages are printed, so ordinary refreshes stay
    // silent.
    if (text && *text) {
        WriteChatf("[Vault] %s", text);
    }
}

// ---------------------------------------------------------------------------
// Selection validation
// ---------------------------------------------------------------------------

bool NmsVaultWnd::ResolveSelection(int* global_slot, int* row_index) const {
    if (!m_pItemList || !global_slot) return false;

    const int row = m_pItemList->GetCurSel();
    if (row < 0 || row >= m_rows) return false;

    const uint32_t data = m_pItemList->GetItemData(row);
    if (data == 0) return false;

    NmsVaultProtocol* proto = NmsVaultProtocol::GetInstance();
    if (!proto) return false;

    const int slot = (int)data;

    // Must be inside the page currently displayed.
    int first = 0, last = 0;
    if (!proto->PageSlotRange(m_page, &first, &last)) return false;
    if (slot < first || slot > last) return false;

    // Must actually hold something.
    if (!proto->Slot(slot).valid) return false;

    *global_slot = slot;
    if (row_index) *row_index = row;
    return true;
}

bool NmsVaultWnd::PageButtonFor(CXWnd* pWnd, int* page_out) const {
    for (int i = 0; i < NMS_VAULT_PAGE_MAX; ++i) {
        if (m_pPageButtons[i] && pWnd == (CXWnd*)m_pPageButtons[i]) {
            if (page_out) *page_out = i + 1;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Outbound
// ---------------------------------------------------------------------------

void NmsVaultWnd::EmitSay(const char* fmt, ...) {
    char body[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    char cmd[288];
    snprintf(cmd, sizeof(cmd), "/say %s", body);

    // Server-side handlers exist for every command this emits (verified in
    // Release-NMS-Quests/global/global_player.pl): #vault_page and
    // #vault_withdraw. Nothing else is sent.
    if (ppEverQuest && pEverQuest && ppLocalPlayer && pLocalPlayer) {
        pEverQuest->InterpretCmd((EQPlayer*)pLocalPlayer, cmd);
    } else {
        SimpleLog("NmsVault: cannot emit [%s], game not ready", cmd);
        return;
    }
    SimpleLog("NmsVault: emitted [%s]", cmd);
}

void NmsVaultWnd::RequestPage(int page) {
    if (page < NMS_VAULT_PAGE_MIN || page > NMS_VAULT_PAGE_MAX) {
        SetStatus("Page out of range.");
        return;
    }

    // Ask the server to render that page. It replies with CLEAR + OPEN|<page> +
    // one ADD per row, which drives OnVaultChanged() and ends up back here via
    // ShowPage().
    //
    // m_page is deliberately NOT changed here. The displayed page only ever moves
    // in ShowPage(), which is driven by the server's OPEN. Until that arrives the
    // window keeps showing the page it is already on, with that page's own slots --
    // so a page-Y label can never appear over page-X contents. The client never
    // invents the contents of a page it has not been sent.
    EmitSay("#vault_page %d", page);
    SetStatus("Requesting page...");
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

int NmsVaultWnd::WndNotification(CXWnd* pWnd, unsigned int Message, void* data) {
    // No titlebar-closebox or Escape branch here on purpose. There is no XWM_CLOSE
    // in this client at all (MQ2Main.h defines only LCLICK/LMOUSEUP/RCLICK and
    // friends), and the XML's Style_Closebox + Escapable are handled by
    // CSidlScreenWnd itself -- which is why WaypointsWnd, with the same XML flags,
    // has no close handler either. The explicit NMSVaultClose button below covers
    // mouse-driven closing.

    if (Message != XWM_LCLICK) {
        return CSidlScreenWnd::WndNotification(pWnd, Message, data);
    }

    int page = 0;
    if (PageButtonFor(pWnd, &page)) {
        if (page != m_page) {
            RequestPage(page);
        }
        return 0;
    }

    if (m_pPrevButton && pWnd == (CXWnd*)m_pPrevButton) {
        if (m_page > NMS_VAULT_PAGE_MIN) {
            RequestPage(m_page - 1);
        }
        return 0;
    }

    if (m_pNextButton && pWnd == (CXWnd*)m_pNextButton) {
        if (m_page < NMS_VAULT_PAGE_MAX) {
            RequestPage(m_page + 1);
        }
        return 0;
    }

    if (m_pCloseButton && pWnd == (CXWnd*)m_pCloseButton) {
        ((CXWnd*)this)->Show(false, false);
        return 0;
    }

    if (m_pItemList && pWnd == (CXWnd*)m_pItemList) {
        int slot = 0;
        if (ResolveSelection(&slot, nullptr)) {
            m_selected_slot = slot;
            RefreshContents();
            Refresh();
        } else {
            m_selected_slot = 0;
            SetStatus("Empty slot.");
        }
        return 0;
    }

    if (m_pWithdrawButton && pWnd == (CXWnd*)m_pWithdrawButton) {
        int slot = 0;
        if (!ResolveSelection(&slot, nullptr)) {
            SetStatus("Nothing selected.");
            return 0;
        }
        // Only action wired up in Patch 8, and it is an ESTABLISHED command:
        // global_player.pl handles #vault_withdraw <global slot> and performs the
        // withdrawal itself. Nothing about the transfer is implemented here.
        EmitSay("#vault_withdraw %d", slot);
        SetStatus("Withdraw requested.");
        return 0;
    }

    return CSidlScreenWnd::WndNotification(pWnd, Message, data);
}
