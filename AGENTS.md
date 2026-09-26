# AGENTS.md

Triptych Triumvirate (NMS) — a LAN-oriented, multiclass EverQuest server (EQEmu 23.8.1 fork, RoF2 client). It is a one-time snapshot and is not maintained upstream.

## Project Identity & Multiclass Architecture

This fork's defining feature is **multiclassing**. The class system is the single most important architectural concept for any code change.

### Core Data Model

| Symbol | Location | Purpose |
|--------|----------|---------|
| `m_pp.class_` | `world/client.cpp:1990` | Legacy single-class field; forced to the Bard sentinel while multiclassing is enabled. Do not assume it represents an owned class in Client gameplay logic. |
| `m_pp.classes` | `common/player_profile.h` | Actual owned-class **bitmask**. This is the source of truth. |
| `Client::GetClassesBits()` | `zone/client.cpp:14576` | Returns `m_pp.classes` (or single-class fallback when multiclassing off). |
| `Mob::HasClass(class_id)` | `zone/mob.cpp:4855` | Mask-aware check: `(classes & GetPlayerClassBit(class_id)) != 0`. Works for single- and multiclass. |
| `player_class_bitmasks[]` | `common/classes.h:79-96` | Explicit map from class ID → bit value. Values are `1 << (class_id - 1)` for playable classes 1-16. |
| `GetPlayerClassBit(class_id)` | `common/classes.cpp:395` | Map lookup returning the bitmask for a given class ID. |
| `GestaltClasses` | Data bucket (`data_buckets` table) | Persistent storage of owned classes; read/written by `Client::AddExtraClass` / `RemoveExtraClass`. |
| `Client::AddExtraClass` / `RemoveExtraClass` | `zone/client.cpp` | Class management with rule/validation checks; updates `m_pp.classes` and data bucket. |

### Class ID vs. Class Bitmask — Critical Invariant

**A numeric class ID is never a class bitmask.** The two are completely different representations:

- Class ID: small integer (e.g., Warrior=1, Cleric=2, Paladin=3, Ranger=4, Bard=8, Wizard=12, etc.)
- Class bitmask: explicit value from `player_class_bitmasks` (Warrior=1, Cleric=2, Paladin=4, Ranger=8, Bard=128, Wizard=2048, etc.)

The values follow `1u << (class_id - 1)` for playable classes 1-16, but **the code uses an explicit map lookup** (`GetPlayerClassBit()`), not a bit-shift formula.

**Example:** `GetPlayerClassBit(12)` returns `2048`, not `12`. A raw class ID passed where a bitmask is expected is a bug.

**Verified bug pattern:** `IsEquipable(race_id, class_bits)` in `common/item_data.cpp:172` expects a **bitmask**. The old `SwapItem` call passed a raw class ID, which coincidentally worked only for classes whose bitmask equals their ID (Warrior ID 1 → bitmask 1). Always verify the expected representation at the callee.

### Multiclass Semantics (Verified)

Semantics vary by subsystem; do not assume a universal rule:

| Subsystem | Semantics |
|-----------|-----------|
| Eligibility (disciplines, AAs, spells) | **Union / any** owned class qualifies |
| HP / endurance | **Best/max** qualifying owned class |
| Mana | Best qualifying **caster** class |
| Skill caps | **Best/max** qualifying owned class |
| Spell eligibility | Owned classes |
| Spell required level | **Minimum** qualifying class level |
| AA eligibility | Multiclass-aware (uses owned classes) |

### `GetClass()` / `GetBaseClass()` Scrutiny Required

In Client gameplay paths, `GetClass()` and `GetBaseClass()` resolve to the **Bard sentinel** when multiclassing is enabled. This is **not universally a bug** — callees may already be mask-aware, or the use may be intentional (e.g., UI display, single-class fallback logic). **Always trace the complete call path** before changing them. The mask-aware helpers are `HasClass()`, `GetClassesBits()`, and `GetPlayerClassBit()`.

### Perl / Lua Interfaces

**Perl** (`zone/perl_client.cpp`):

| API | Implementation | Returns |
|-----|----------------|---------|
| `GetClassBitmask()` | `GetPlayerClassBit(self->GetClass())` | **Single class bit** (Bard sentinel when multiclassing) |
| `GetClassesBitmask()` | `self->GetClassesBits()` | **Full multiclass bitmask** |
| `HasClassID(int)` | `self->HasClass(class_id)` | Mask-aware |
| `HasClass(string)` | `GetPlayerClassIDByName(name) → HasClass(id)` | Mask-aware |
| `GetClassAbbreviation()` | `GetPlayerClassAbbreviation(self->GetBaseClass())` | Uses Bard sentinel |

**Lua** (`zone/lua_client.cpp`):

| API | Implementation | Returns |
|-----|----------------|---------|
| `GetClassBitmask()` | `self->GetClassesBits()` | **Full multiclass bitmask** |
| `GetClassesBitmask()` | `self->GetClassesBits()` | **Full multiclass bitmask** |
| `HasClassID(int)` | `self->HasClass(class_id)` | Mask-aware |
| `GetClassAbbreviation()` | Uses `GetClass()` | Uses Bard sentinel |

**Critical Inconsistency (scripting trap):**
- **Perl:** `GetClassBitmask()` (singular) ≠ `GetClassesBitmask()` (plural) — single vs multiclass
- **Lua:** `GetClassBitmask()` == `GetClassesBitmask()` — both return full multiclass mask

---

## Build and Run (Windows)

**Server** — use `Release-NMS-Server\build-windows.bat`; it locates CMake via `vswhere`, auto-detects the installed Visual Studio generator, and supports an `EQEMU_GENERATOR` environment-variable override.

```
cd Release-NMS-Server
build-windows.bat
```

Equivalent manual command (generator auto-detected):

```
cmake -S . -B Build -G "<auto>" -A x64 -DEQEMU_BUILD_LOGIN=ON
cmake --build Build --config Release
```

**Requirements:** Any C++20-capable MSVC (VS 2019 16.11+, VS 2022, VS 2026). The script detects the generator from `cmake --help`; VS 2022 is **not** a hard requirement.

Output lands in `Build\bin\Release\`. The committed `bin\Release\` prebuilts are stale (lack `zone`/`ucs` LAN customizations) — rebuild; don't trust them.

**First configure** needs internet — CMake pulls ~132 MB of prebuilt deps into `vcpkg\`.

**Client DLL** — must be 32-bit, full rebuild:

```
cd Release-NMS-Client
msbuild eqgame_dll.sln /t:Rebuild /p:Configuration=Release /p:Platform=Win32
```

- Win32 only; x64 will not load in RoF2 client.
- Always `/t:Rebuild`; incremental builds link stale objects.
- Output: `Release\dinput8.dll` next to `eqgame.exe`.

**Dead scripts:** `build_server.bat`, `rebuild_server.bat` at repo root hardcode `C:\EQS\Release-NMS-Server` and a `Visual Studio\18\Community` CMake path. Do not run or silently repair them.

**Server control scripts** (`start-servers.bat`, `stop-servers.bat`, `shared-memory.bat`, `export-client-files.bat`) resolve `%~dp0Release-NMS-Server\Build\bin\Release` — they fail until you have built locally.

`start-servers.bat` also needs Perl at `Release-NMS-Server\perl\x64\perl` and MariaDB client tools on `PATH`.

---

## Dependency Discovery

The top-level `CMakeLists.txt` selects the Lua implementation and publishes the selected paths:

- `FIND_PACKAGE(Lua51)` and `FIND_PACKAGE(LuaJit)` both run.
- Selection logic sets `LUA_LIBRARY_INCLUDE` = `${LUAJIT_INCLUDE_DIR};${CMAKE_CURRENT_SOURCE_DIR}/libs/luabind` (or Lua 5.1 equivalent).
- **Never bypass this abstraction** by using `LUA_INCLUDE_DIR` or `LUAJIT_INCLUDE_DIR` directly in subdirectories. That was the bug fixed in `zone/CMakeLists.txt:283` — always consume `LUA_LIBRARY_INCLUDE`.

Database: both `MySQL` and `MariaDB` finders run. `MySQL: MISSING` in the configure output is **informational only** when MariaDB is successfully selected (it provides `libmariadb.lib` and headers under `include/mysql/`). The project correctly falls back to MariaDB.

---

## Database Changes — Critical Workflow

This fork maintains its schema through a hand-maintained manifest. **A SQL change requires two coordinated edits:**

1. Append a `ManifestEntry` to `common/database/database_update_manifest_custom.cpp` with `.version` = next integer.
2. Bump `CUSTOM_BINARY_DATABASE_VERSION` in `common/version.h` (currently **78**).

Why both: `common/database/database_update.cpp:159` iterates `version_low + 1 .. version_high`. A gap skips that migration forever. A bump with no entry stamps the DB current without applying anything. `server.auto_database_updates` is `true` in `eqemu_config.json.example`.

Each entry needs:
- Idempotent `check` query + `condition` (`empty`|`missing`|`match`|`contains`|`not_empty`)
- Correct `content_schema_update` flag (true only for content-schema objects)
- Comment block explaining the **reason**, not just the DDL

New `Custom:*` rule defaults go into `rule_values` **via a manifest entry**, not `eqemu_config.json`.

`server.content_database` is intentionally blank — one schema holds both player and content objects.

**Audit after content changes:**

```bash
mysql -u <user> -p <dbname> < Release-NMS-Server/utils/sql/nms_content_health_check.sql
```

Read-only, safe to repeat; every line prints its expected value. **Note:** the health check's header says "Expected custom_version: 63" but `common/version.h:47` is **78**; v64–v78 have no audit coverage. Read the number from `version.h`.

---

## Repo Shape

| Path | What it is |
| --- | --- |
| `Release-NMS-Server/` | C++ server: `world` `zone` `ucs` `queryserv` `loginserver` `eqlaunch` + committed prebuilts in `bin/Release/` + DB dump in `database/release-peq.zip` |
| `Release-NMS-Client/` | `dinput8.dll` client proxy (C++, Win32) + `ClientFiles/` UI overlay |
| `Release-NMS-Quests/` | Quest scripts, Perl and Lua, one folder per zone |
| `Release-NMS-Plugins/` | Perl plugins the quests load |

Fork-specific code is concentrated in: `Release-NMS-Server/zone/` and `world/`, `common/database/database_update_manifest_custom.cpp` (**all** content SQL ships from here), `Release-NMS-Plugins/NMS_*.pl`, and `Release-NMS-Quests/lua_modules/`. Everything else is upstream EQEmu — read it for context, don't restyle it.

- `Release-NMS-Server/GM-COMMANDS.md` is the authoritative `#command` list with status levels and NMS side effects. `#help` in-game beats it.
- `Release-NMS-Plugins/deity_blessings.pl` (`%BLESS_PROC`) is the source of truth for the deity proc table in the root README. If the two disagree, the `.pl` wins.
- `Release-NMS-Client/eqgame_dll/_options.h` holds every client behaviour toggle, one commented flag per feature.

**Custom rules:** 112 `Custom:*` rules defined in `common/ruletypes.h`; 81 unique rules read across ~300 call sites (`RuleB`/`RuleI`/`RuleF`/`RuleS`/`RuleR`).

---

## Quests and Plugins

Deploy paths per `eqemu_config.json.example` `server.directories`:

- `Release-NMS-Quests/*` → `quests/` (folder **contents**, not the folder itself)
- `Release-NMS-Plugins/*` → `quests/plugins/`
- `Release-NMS-Quests/lua_modules/` → `quests/lua_modules/`

Style rules that differ from defaults you might assume:

- Tabs, width 8, in both Perl and Lua.
- Spaces after list items and method parameters, and around operators and hash arrows: `[1, 2, 3]` not `[1,2,3]`; `x += 1` not `x+=1`.
- **Always comment database IDs** — item, spell, NPC, faction, and task IDs. Reviewers rely on it.
- Prefer Data Buckets over Quest Globals for anything persisted; Globals are far slower.
- The same quest often exists as both `.pl` and `.lua` in one zone folder. Change both or neither.
- `highpasshold.DISABLED` is the convention for parking a zone's quests. Observed once, not documented — check the zone's loader before assuming `.DISABLED` is honoured.

---

## Verification

No test suite, no CI. Existing checks:

- `Release-NMS-Quests/check` — POSIX shell; runs `perl -c` and `luac -p` over every script. Needs Git Bash or WSL on Windows.
- `Release-NMS-Server/utils/sql/nms_content_health_check.sql` — DB audit above.

`Release-NMS-Server/tests/` is upstream cppunit over `common/` utilities only; `EQEMU_BUILD_TESTS` defaults to `OFF` (`CMakeLists.txt:167`). Neither build script passes the flag, so `tests.exe` is not produced. Add `-DEQEMU_BUILD_TESTS=ON` if you want it.

In-game: `#reload quests` (alias `#rq`) reloads scripts without restart; `GM-COMMANDS.md` lists the rest.

C++ style: tabs for `.cpp`/`.h`, LF endings, per `.editorconfig`. MSVC warnings are off (`/W0`), so unused-variable warnings will not catch mistakes.

**Build verification:** A clean checkout can now configure and fully build in Release with VS 18 2026 / MSVC 14.51 on this machine. After any change, run the normal CMake build when practical.

---

## Known Doc Drift — Trust Config Over Prose

Verified mismatches; executable source wins:

- **C++ standard:** `CMakeLists.txt:15` sets `CMAKE_CXX_STANDARD 20`. READMEs and `.vscode/c_cpp_properties.json` say C++17. **It is 20.**
- **Login port:** `eqemu_config.json.example` uses `5998`. `start-servers.bat` echoes "5999/udp". Trust the config file.
- **Exporter path:** `Release-NMS-Client/README.md:85` says exporter is in `bin\Release\`; `export-client-files.bat` uses `Build\bin\Release\`. Both correct for their case (prebuilt vs fresh).
- **Health check stale:** `utils/sql/nms_content_health_check.sql` says "Expected custom_version: 63" and audits v18–v63; `common/version.h:47` is **78**. v64–v78 have no audit coverage. Read the number from `version.h`.
- **Missing docs:** `Release-NMS-Server/README.md:134` links to `../Release-NMS-Deploy/CODEBASE.md` and `GM-COMMANDS.md` to `reference/GM Commands.md`. Neither file exists.
- **Rule counts:** Old AGENTS.md said "38 distinct `Custom:*` rules read". Actual: **112 defined, 81 read, ~300 sites**.
- `Release-NMS-Quests/CONTRIBUTING.md` is inherited upstream prose from Gates of Time. Only its four style rules are current.

---

## Traps

- **Do not "restore" three commented-out fork blocks.** They are deliberate LAN customizations; an upstream-diff reading makes them look like dead code:
  - `zone/client_process.cpp` — `RuleB(Custom, ServerAuthStats)` client-version warning and Bazaar transfer. The rule itself is live and used by multiclass features.
  - `ucs/worldserver.cpp:95,104` and `ucs/ucs.cpp:175` — Discord webhook queue calls and `PlayerEventQueueListener` thread. Function bodies exist; only calls and thread are disabled. Ordinary chat/mail and player-event logging unaffected.
- **Regenerate the four DB-derived client files** after any change to spells, skills, or item text: `spells_us.txt`, `dbstr_us.txt`, `SkillCaps.txt`, `BaseData.txt`. Run `export-client-files.bat <client-folder>` from repo root. They must land in **both** the client root and the client's `Resources\` folder or the client loads stale data. They are gitignored.
- `Release-NMS-Client/ClientFiles/` is an **overlay**, not a client — the same XML changes are replicated across `uifiles\default`, `gearcore`, `shinsparxx`, and `Blue`. Keep skins in sync.
- `Release-NMS-Quests/scripts/findapocdb/main.go:25` has a hardcoded DB password committed. Treat it as compromised; don't reuse, copy, or add more.
- `Release-NMS-Quests/go.mod`/`go.sum` belong to two standalone `scripts/findapoc*` Go helpers, not to the quests. Nothing in the quest tree is Go.
- `submodules/` is vendored source, not git submodules — no `.gitmodules`; `git submodule update --init --recursive` (in `.devcontainer/Makefile`) is a no-op.
- **Gitignored, never commit:** `Release-NMS-Server/eqemu_config.json`, `login.json`, `opencode.json`, `spire.exe`, `release-peq.sql`, `export/`, `backups/`, `Build/`, `vcpkg/`, `perl/`, `logs/`. Only `.example` configs are tracked.
- `Release-NMS-Server/maps/` is tracked. Repo-root `/maps/` is not, and is ignored.
- **Do not bypass `LUA_LIBRARY_INCLUDE`** by using `LUA_INCLUDE_DIR` or `LUAJIT_INCLUDE_DIR` directly in subdirectories. That was the bug fixed in `zone/CMakeLists.txt:283`.

---

## Development Practices

- Investigate before editing. Prefer existing NMS/EQEmu helpers and patterns.
- Make minimal, targeted changes. Avoid unrelated refactoring.
- Trace `GetClass()`/`GetBaseClass()` uses rather than mechanically replacing them.
- Verify class ID vs. class mask expectations at API boundaries.
- Build affected targets after changes; run the normal CMake build when practical.
- Report unrelated/pre-existing failures instead of silently fixing them.
- Do not commit or push unless explicitly requested.

---

## Git Workflow

- `upstream` = `Russianranger/Triptych-Triumvirate` (source of truth; branch `main`)
- `origin` = `GreyMouser69/Triptych-Triumvirate` (our fork)
- `upstream/main` is the upstream source branch
- `nms-development` is the branch currently used for our modifications
- Keep local `master` aligned with the upstream baseline. Make development changes on `nms-development` or another development branch.
- Recent history is mostly `type(scope): summary` conventional commits, interleaved with GitHub web-UI commits. Match what's recent.
- **Do not commit or push unless explicitly requested.**