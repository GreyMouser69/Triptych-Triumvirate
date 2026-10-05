/**
 * @file nms_loot_window.h
 * @brief NMS-LOCAL: native pending-loot and looter-roster window (Patch 9).
 *
 * A VIEW over Patch 7's authoritative models, exactly as nms_vault_window is a view
 * over NmsVaultProtocol. This file:
 *   - does NOT parse 0x140A or 0x140D,
 *   - does NOT keep a second pending-offer or roster cache,
 *   - does NOT build or send a 0x140B packet itself.
 *
 * It calls NmsLootProtocol::SendReply() for actions and reads NmsLootProtocol's
 * Pending()/Roster() for display. That is the whole contract.
 *
 * FOLLOWS THE PROVEN Triptych pattern, mirroring nms_vault_window (Patch 8):
 *   - CCustomWnd subclass, lazily constructed on first show
 *   - GetChildItem() control binding in the constructor
 *   - SetWndNotification + WndNotification for input
 *   - AddXMLFile in Initialize(), delete in Shutdown()
 *   - NMS_DestroyCustomWindows() in MQ2CleanUI.cpp for /loadskin teardown
 *   - OnSetGameState fan-out from MQ2Pulse.cpp
 *
 * The plan (section 8.5) explicitly prefers native list controls over an
 * unproven icon-grid, so this is two lists plus buttons.
 *
 * VISIBILITY follows the evidence in BFE docs/NMSLOOT_PROTOCOL.md:534 -- when an
 * offer arrives the client announces it and the item appears in the decision queue.
 * A newly VALID, NON-EMPTY pending packet is therefore the open trigger, and a
 * zero-count packet only clears. That is modelled with an explicit change reason,
 * not by polling vector size, so a user who closed the window does not get it
 * reopened by an unrelated background refresh.
 */

#pragma once

#include "MQ2Main.h"
#include "nms_loot_protocol.h"

#include <string>
#include <vector>

class NmsLootWnd : public CCustomWnd {
public:
    NmsLootWnd();
    // No `override`: CCustomWnd's destructor is not virtual in this binding
    // (MQ2Internal.h), so the specifier is a hard error. Same as NmsVaultWnd.
    ~NmsLootWnd();

    // No `override`: installed into the vtable slot by SetWndNotification()
    // (MQ2Main.h:170), exactly as the waypoint and vault windows do it.
    int  WndNotification(CXWnd* pWnd, unsigned int Message, void* data);

    // ---- lifecycle ---------------------------------------------------------
    static void Initialize();      // AddXMLFile + /nmsloot
    static void Shutdown();        // RemoveCommand + delete + null
    static void OnSetGameState(int state);

    // ---- Patch 7 model notification ---------------------------------------
    //
    // Why is a change reason needed at all? Patch 8 hit exactly this: the verbs
    // have different UI lifetimes. Here:
    //   - a new non-empty pending packet OPENS the window (a decision is waiting),
    //   - a zero-count packet only clears it,
    //   - a roster packet only refreshes, and must never open anything,
    //   - a local reply only refreshes.
    // Inferring "open" from Pending().size() > 0 after every event would pop the
    // window back open after the player deliberately closed it.
    enum Change {
        CHANGE_PENDING_NEW,   // valid non-empty 0x140A: create + show + refresh
        CHANGE_PENDING_CLEAR, // zero-count 0x140A: clear only, never create
        CHANGE_ROSTER,        // 0x140D: refresh an existing window only
        CHANGE_LOCAL_REPLY    // a row we just submitted: refresh only
    };

    static void OnLootModelChanged(Change why);

    // Manual open/refresh, used by /nmsloot. Shows when there is something to
    // decide; otherwise reports why there is nothing to show.
    static void Toggle();

    static NmsLootWnd* GetInstance();

private:
    void RefreshPending();
    void RefreshRoster();
    void RefreshActionButtons();
    bool ActionButtonFor(CXWnd* pWnd, uint8_t* action_out) const;
    bool RosterButtonFor(CXWnd* pWnd, int* index_out) const;

    // Resolve the selected row against the AUTHORITATIVE model. Returns false for
    // an out-of-range selection, an offer the model no longer holds, or a row
    // already submitted -- so no action can be sent for a stale or spent row.
    bool ResolveSelection(uint32_t* offer_id) const;

    // Send one action through Patch 7. `target` is used only by Pass.
    bool SubmitAction(uint8_t action, const char* target);

    void SetStatus(const char* text);

    static NmsLootWnd* s_instance;

    // ---- controls ----------------------------------------------------------
    CListWnd*   m_pPendingList;
    CListWnd*   m_pRosterList;
    CButtonWnd* m_pKeepButton;
    CButtonWnd* m_pSellButton;
    CButtonWnd* m_pTributeButton;
    CButtonWnd* m_pBankButton;
    CButtonWnd* m_pVaultButton;
    CButtonWnd* m_pDestroyButton;
    CButtonWnd* m_pPassButton;
    CButtonWnd* m_pCloseButton;

    // ---- transient presentation state only ---------------------------------
    // Both are just indices/IDs used to re-find rows in the authoritative model.
    // Neither caches any authoritative data.
    int  m_selected_row;
    int  m_selected_roster;   // -1 = none
    int  m_pending_rows;
    int  m_roster_rows;
};
