/**
 * @file nms_vault_window.h
 * @brief NMS-LOCAL: native Dimensional Pocket Vault window (Patch 8).
 *
 * DISPLAY + ESTABLISHED-COMMAND EMISSION ONLY.
 *
 * This window is a VIEW over the model Patch 7 already owns
 * (NmsVaultProtocol). It never parses VAULTDATA, never keeps a second copy of
 * vault contents, and never invents storage state -- the server stays
 * authoritative. Everything shown here is read from NmsVaultProtocol on refresh.
 *
 * FOLLOWING THE EXISTING PATTERN. Modelled directly on waypoint_window.h, which
 * is this fork's only SIDL-template window:
 *   - CCustomWnd subclass, lazily constructed on first show
 *   - controls bound once in the constructor via GetChildItem()
 *   - SetWndNotification + WndNotification for input
 *   - Initialize() calls AddXMLFile(); Shutdown() deletes the instance
 *   - NMS_DestroyCustomWindows() in MQ2CleanUI.cpp tears it down on /loadskin
 *     and camp-to-char-select, so no control pointer is left dangling
 *
 * PAGE MODEL is Patch 7's, unmodified. Slot numbers are GLOBAL:
 *     pages 1-6  slots (page-1)*10+1 .. page*10     general
 *     page  7    61..70   clicky 1
 *     page  8    71..80   clicky 2
 *     page  9    81..83   special (Proc Locker Pri/Sec/Rng)
 * Page 9 has only three real slots; the remaining seven grid cells are rendered
 * as disabled and can never be selected or acted on.
 *
 * The plan (section 8.3) explicitly prefers "robust native list controls rather
 * than an unproven icon-grid implementation", so this uses two list controls
 * (top-level rows, and the selected container's contents) plus page buttons 1-9.
 */

#pragma once

#include "MQ2Main.h"
#include "nms_vault_protocol.h"

#include <string>
#include <vector>

class NmsVaultWnd : public CCustomWnd {
public:
    NmsVaultWnd();
    // No `override`: CCustomWnd's destructor is not declared virtual in this
    // binding, so the specifier is a hard error (C3668). Same as WaypointsWnd.
    ~NmsVaultWnd();

    // No `override`: SetWndNotification() installs this into the vtable slot
    // directly (MQ2Main.h:170), and WaypointsWnd declares it the same way.
    int  WndNotification(CXWnd* pWnd, unsigned int Message, void* data);

    // ---- lifecycle, mirroring WaypointsWnd ---------------------------------
    static void Initialize();     // AddXMLFile, once
    static void Shutdown();       // delete + null, safe when never created
    static void OnSetGameState(int state);

    // Called by the Patch 7 protocol layer after an accepted command. OPEN creates
    // and shows; ADD/CLEAR refresh only, and are never forwarded at all when no
    // window exists.
    //
    // `why` is NmsVaultChange, declared in nms_vault_protocol.h because the
    // protocol layer is the caller and has to name the verb it just accepted.
    static void OnVaultChanged(NmsVaultChange why);

    // Show page N if the protocol says the vault is open. Creates the window if
    // needed. Safe to call before the window has ever been constructed.
    static void ShowPage(int page);

    static NmsVaultWnd* GetInstance();

private:
    void Refresh();
    void RefreshRow(int index);
    void RefreshContents();
    void RefreshPageButtons();
    bool PageButtonFor(CXWnd* pWnd, int* page_out) const;

    // Validate a slot against the CURRENT model. Returns false for an empty slot,
    // a slot outside the displayed page's window, or a slot that does not exist.
    bool ResolveSelection(int* global_slot, int* row_index) const;

    // The only outbound path. Emits through the established InterpretCmd
    // transport, exactly as the plan section 8.3 specifies.
    void EmitSay(const char* fmt, ...);
    void RequestPage(int page);

    // Status text goes to chat, not a label -- see the control note above.
    void SetStatus(const char* text);

    static NmsVaultWnd* s_instance;

    // ---- controls, bound in the constructor -------------------------------
    //
    // NOTE on the two labels: SIDL <Label> pieces expose their text through
    // CStaticTextTemplate, which this binding does not surface on CXWnd, and
    // WaypointsWnd binds labels only to toggle the Enabled flag -- it never
    // rewrites label text at runtime either. There is no established
    // CXWnd::SetText in this client. So the page indicator and status line are
    // NOT labels: they are buttons, which DO have text and a working
    // CButtonWnd::SetCheck() for the active-page indicator. Status text is
    // surfaced through chat instead (see SetStatus).
    CListWnd*    m_pItemList;
    CListWnd*    m_pContentsList;
    CButtonWnd*  m_pPageIndicator;      // shows "Page N of 9 (kind)"
    CButtonWnd*  m_pPageButtons[NMS_VAULT_PAGE_MAX];   // 9 entries
    CButtonWnd*  m_pPrevButton;
    CButtonWnd*  m_pNextButton;
    CButtonWnd*  m_pWithdrawButton;
    CButtonWnd*  m_pCloseButton;

    // ---- transient view state ONLY (never authoritative) ------------------
    int  m_page;             // displayed page, 1..9
    int  m_selected_slot;    // 0 = nothing selected
    int  m_rows;             // rows currently in m_pItemList
    int  m_contents_rows;    // rows currently in m_pContentsList
};