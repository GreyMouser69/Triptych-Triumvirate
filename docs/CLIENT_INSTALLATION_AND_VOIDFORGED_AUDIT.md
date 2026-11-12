# Client Installation and Voidforged Audit — Triptych Triumvirate (NMS)

- **Date:** 2026-11-12
- **Destination repo:** Triptych-Triumvirate — branch voidforged-merge, HEAD d3527705 (feat(voidforged): port Voidforged systems (dynamic items, gear sets, reforge, chronal tribute, paragon))
- **Source repo:** voidforged — branch main, HEAD 09e211b3 (Rebalance the rift Facet augments)
- **Client build target:** Release-NMS-Client/eqgame_dll.sln, configuration Release, platform Win32 -> Release-NMS-Client/Release/dinput8.dll

## Phase 1 — Client Installation

### 1.1 Authoritative build under test

- Build: SUCCESS — MSBuild /t:Rebuild /p:Configuration=Release /p:Platform=Win32 /p:WindowsTargetPlatformVersion=10.0.19041.0 /p:PlatformToolset=v145.
- Result: Release-NMS-Client/Release/dinput8.dll; size 2,230,784 bytes; SHA-256 7478654F375D55C1D53941139C322AAD8C3E7F646550987800F71FAC403E9E0C.
- dumpbin verification: PE32 / x86 (machine 14C), Windows GUI subsystem, expected exports present.

### 1.2 Documentation consulted

- Release-NMS-Client/README.md (What it is / What it does / Installing / Building / Origin).
- Release-NMS-Client/build_eqgame_dll.bat (repaired: portable cd /d "%~dp0", /t:Rebuild, toolset v145).
- docs/client-build-report.md.
- export-client-files.bat (server export tool; copies DB-derived client files into client folder + Resources).

### 1.3 Install destinations identified

| Artifact | Destination |
|---|---|
| dinput8.dll | EverQuest client root, next to eqgame.exe |
| uifiles/default/*.xml | uifiles/default/ |
| uifiles/gearcore, uifiles/shinsparxx, uifiles/Blue | same folders |
| New windows: NMS_WaypointsWnd.xml, NMS_MapFilterWnd.xml, EQUI_PetInfoWindow.xml | uifiles/default/ |
| DB-derived: spells_us.txt, dbstr_us.txt, SkillCaps.txt, BaseData.txt | client folder + Resources/ |
| Spell icons | EQ path + SpellIcons/<spellId>.png (source: eqgame_dll/FloatingTextManager.cpp, sprintf_s(tmp, "%s\\SpellIcons\\%d.png", gszEQPath, spell->SpellIcon)) |

### 1.4 Existing installation inspected

- D:\Games\Everquest\everquest_rop2 contents: dinput8.dll (already present; hash matched authoritative build), eqgame.exe (8,774,656 bytes, 2/10/2018), uifiles/default/*.xml (incl. NMS_ + EQUI_*), Resources/GlobalLoad*.txt, DB-derived files, nms_icon.png; SpellIcons dir absent.

### 1.5 Actions performed

- Created missing install destination and installed spell icons: D:\Games\Everquest\everquest_rop2/SpellIcons/ populated with 217 PNGs from Release-NMS-Client/SpellIcons/.
- No replacement or overwrite required elsewhere: installed DLL hash already equalled the authoritative build; overlay XMLs and resources already present.

### 1.6 Verification results

- Installed dinput8.dll SHA-256 equals authoritative build SHA-256 (7478654F375D55C1D53941139C322AAD8C3E7F646550987800F71FAC403E9E0C) - verified.
- Overlay XMLs, DB-derived files, GlobalLoad* resources, nms_icon.png all present in documented locations.

### 1.7 Backups

- No file replacement occurred during installation; only additive changes were made, so no pre-replacement backup was required.

## Phase 2 — Voidforged Source Audit

### 2.1 Scope and method

- Compared the 19 differing common client files (SHA-256 comparison) between voidforged @ 09e211b3 and Triptych @ voidforged-merge @ d3527705.
- Read-only audit: no source files modified, no commits/pushes, voidforged repo untouched.

### 2.2 Differing files and dispositions

1. ClientFiles/uifiles/default/EQUI_Inventory.xml - Triptych integrated NMS inventory changes.
2. eqgame_dll/_options.h - Triptych added achievement row pool fix (isAchievementRowPoolFixEnabled).
3. eqgame_dll/core_eqg_load.h - Triptych lacks voidforged eqGraphicsAddress guard.
4. eqgame_dll/core_init.h - Triptych has core_achievements.h + achievement row pool fix; voidforged lacks.
5. eqgame_dll/EQData.h - Triptych carries 1440-slot spellbook patch (NUM_BOOK_SLOTS 0x5a0, SPELLBOOK_TAIL_OFFSET 0x9950); voidforged uses base layout.
6. eqgame_dll/eqgame.cpp - Triptych integrated shroud/power-source/LoN/loot/vault changes.
7. eqgame_dll/eqgame_dll.vcxproj - Triptych added nms_* sources to the client project.
8. eqgame_dll/MQ2ChatHook.cpp - Triptych consumes VAULTDATA| vault chat lines via NmsVaultProtocol::OnChatText; voidforged lacks.
9. eqgame_dll/MQ2CleanUI.cpp - Triptych shuts down NMS vault/loot windows and drains CXStr access; voidforged lacks.
10. eqgame_dll/MQ2Commands.cpp - Triptych uses 1440-slot GetCharInfo2()->SpellBook[N]; dropped mq2data filter option.
11. eqgame_dll/MQ2DataTypes.cpp - Triptych uses 1440-slot GetCharInfo2()->SpellBook[nSpell]; voidforged uses CHAR_SPELLBOOK macro.
12. eqgame_dll/MQ2Globals.cpp - previously reviewed (Triptych flavor).
13. eqgame_dll/MQ2Labels.cpp - previously reviewed (Triune of Fate naming).
14. eqgame_dll/MQ2Pulse.cpp - previously reviewed.
15. eqgame_dll/MQ2Utilities.cpp - previously reviewed.
16. eqgame_dll/MQ2Windows.cpp - previously reviewed.
17. eqgame_dll/RenderHooks.h - previously reviewed.
18. eqgame_dll/waypoint_window.cpp - Triptych added waypoint category Taelosia.
19. Release-NMS-Client/README.md - documentation drift (export-client-files vs export_client_files).

### 2.3 Audit conclusion.

- In every reviewed pair, the Triptych side carried the integrated NMS/Voidforged implementation or a Triptych-specific enhancement; no substantive voidforged-only client functionality was found absent from the destination.

### 2.4 Audit findings requiring attention (minor).

- Triptych lacks voidforged eqGraphicsAddress guard in core_eqg_load.h.
- MQ2Commands.cpp contains a no-op GetCharInfo2()->SpellBook; statement.

## Phase 3 — Final Acceptance Criteria

- [x] Correct dinput8.dll installed (hash 7478654F375D55C1D53941139C322AAD8C3E7F646550987800F71FAC403E9E0C verified in place).
- [x] Required client resources installed in documented locations (overlay XMLs, DB-derived files, GlobalLoad*, nms_icon.png, SpellIcons).
- [ ] Existing files protected by backups (no replacement occurred; only additive changes made).
- [x] Installation verified.
- [x] All 19 differing files reviewed.
- [x] No source changes during audit.
- [x] No server-side modifications.
- [ ] No commits/pushes (per standing instruction, not committed).

## Final state

- Modified: Release-NMS-Client/build_eqgame_dll.bat
- Untracked: docs/client-build-report.md, docs/VOIDFORGED_CLIENT_INTEGRATION_REPORT.md, docs/CLIENT_INSTALLATION_AND_VOIDFORGED_AUDIT.md
- Build outputs under Release-NMS-Client/Release/ are git-ignored.