-- ============================================================================
-- Triptych content health check - verifies the DATA each custom-manifest
-- version is supposed to deliver, without trusting db_version. It contains
-- targeted checks for selected payloads from v18-v42, v50 (the alternate-
-- currency self-heal), v62-v63 (the Fabled season schema), v71 (Triune blessing
-- names), v80 (the vault/loot player-schema objects and rule rows), and v81 (the
-- on-demand Vault Speaker content row). It is not exhaustive coverage of every
-- feature in those versions and does not audit
-- v43-v49, v51-v61, v64-v70, or v72-v79.
--
-- Why this exists: we have now twice found servers whose custom_version was
-- stamped PAST an entry whose content never landed (a half-apply healed by a
-- later resync; a wholesale-skip healed by the v23-25 repairs). The version
-- number is a claim; this file is the audit. Every line prints its own
-- expectation - anything that misses its expected value identifies exactly
-- which payload is absent.
--
-- custom_version is printed for context only. This file does not assert a
-- version ceiling because it does not inspect every custom migration.
--
-- Run (Windows / MariaDB):
--   "C:\Program Files\MariaDB 12.3\bin\mariadb.exe" -u <user> -p <dbname> < nms_content_health_check.sql
-- Or from any mysql/mariadb client: source nms_content_health_check.sql
--
-- Single-DB assumption: this fork runs with `content_database` unset, so all
-- player-schema and content-schema objects live in the one database. If a
-- split deployment ever runs this file, player-schema rows (account, trader,
-- character_illusions, ...) will not exist in the content DB.
--
-- READ-ONLY: SELECT/SHOW only. Safe on any server, any number of times.
-- ============================================================================

SELECT 'db_version.custom_version (informational; no expected value)' AS what, custom_version AS value FROM db_version LIMIT 1;

-- ---- v18 / v23: Beastlord spell merchant + scrolls -------------------------
SELECT 'v23 bl merchant npc (expect 1)' AS what, COUNT(*) AS value FROM npc_types WHERE id = 1120001300;
SELECT 'v23 bl scrolls (expect 38)' AS what, COUNT(*) AS value FROM merchantlist WHERE merchantid = 1120001300;

-- ---- v18 / v24: merchant spawns --------------------------------------------
SELECT 'v24 spawngroups (expect 2)' AS what, COUNT(*) AS value FROM spawngroup WHERE name IN ('fv_Beastlord_Spell_Merchant','ot_Beastlord_Spell_Merchant');
SELECT 'v24 spawn2 rows (expect 2)' AS what, COUNT(*) AS value FROM spawn2 WHERE spawngroupID IN (SELECT id FROM spawngroup WHERE name IN ('fv_Beastlord_Spell_Merchant','ot_Beastlord_Spell_Merchant'));

-- ---- v18 / v25: data payload ------------------------------------------------
SELECT 'v25 whitelist has aa516 (expect 1)' AS what, COUNT(*) AS value FROM rule_values WHERE rule_name = 'Custom:AA339Whitelist' AND rule_value LIKE '%aa516%';
SELECT 'v25 bl GMs class (expect 34,34)' AS what, GROUP_CONCAT(class) AS value FROM npc_types WHERE id IN (93152, 84202);

-- ---- v19 / v22: factions + rules --------------------------------------------
SELECT 'v19 factions (expect 929,929,929,929)' AS what, GROUP_CONCAT(npc_faction_id) AS value FROM npc_types WHERE id IN (46016,46017,46061,46089);
SELECT 'v19 faction 2000507 (expect 79)' AS what, npc_faction_id AS value FROM npc_types WHERE id = 2000507;
SELECT 'v19 buy cost mod (expect 1.0)' AS what, rule_value AS value FROM rule_values WHERE rule_name = 'Merchant:BuyCostMod';
SELECT 'v19 db_str 15594 fixed (expect 1)' AS what, COUNT(*) AS value FROM db_str WHERE id = 15594 AND type = 4 AND value LIKE '%additional damage%';

-- ---- v20: Sateal in the Bazaar -----------------------------------------------
-- v20 runs a blanket UPDATE on name='Sateal_Deirosap'; the dump carries two such
-- NPCs (151001 and 151200), so the correct count is 2.
SELECT 'v20 sateal titled (expect 2)' AS what, COUNT(*) AS value FROM npc_types WHERE name = 'Sateal_Deirosap' AND lastname = 'Smithing Supplies';

-- ---- v21 / v22: data payload --------------------------------------------------
SELECT 'v22 bazaar cancombat (expect 0,0)' AS what, GROUP_CONCAT(cancombat) AS value FROM zone WHERE short_name = 'bazaar';
SELECT 'v22 bazaar safe_x (expect -134.13 x2)' AS what, GROUP_CONCAT(safe_x) AS value FROM zone WHERE short_name = 'bazaar';
SELECT 'v22 hastened AA (expect 1600,2000)' AS what, GROUP_CONCAT(base1) AS value FROM aa_rank_effects WHERE rank_id IN (12899,12900) AND slot = 1 AND base2 = 57;
SELECT 'v22 aa next_id (expect -1)' AS what, next_id AS value FROM aa_ranks WHERE id = 12900;
SELECT 'v22 quegmor moved (expect -76.12)' AS what, ROUND(z,2) AS value FROM spawn2 WHERE id = 14745;

-- ---- v26: Bazaar armour-glamour NPC -------------------------------------------
SELECT 'v26 glamour npc (expect 1)' AS what, COUNT(*) AS value FROM npc_types WHERE id = 1120001110 AND name = 'Purveyor_of_Armour_Glamour';
SELECT 'v26 glamour spawngroup (expect 1)' AS what, COUNT(*) AS value FROM spawngroup WHERE id = 5003550;
SELECT 'v26 glamour spawn2 (expect 1)' AS what, COUNT(*) AS value FROM spawn2 WHERE id = 2141650;

-- ---- v27: account offline flag -------------------------------------------------
SELECT 'v27 account.offline col (expect 1)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'account' AND COLUMN_NAME = 'offline';

-- ---- v28: trader offline schema -------------------------------------------------
SELECT 'v28 trader.character_id col (expect 1)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'trader' AND COLUMN_NAME = 'character_id';
SELECT 'v28 trader.item_unique_id col (expect 1)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'trader' AND COLUMN_NAME = 'item_unique_id';

-- ---- v29: item_unique_id on inventory / sharedbank -------------------------------
SELECT 'v29 inventory.item_unique_id col (expect 1)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'inventory' AND COLUMN_NAME = 'item_unique_id';
SELECT 'v29 sharedbank.item_unique_id col (expect 1)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'sharedbank' AND COLUMN_NAME = 'item_unique_id';

-- ---- v30: parcels item_unique_id + evolve_amount --------------------------------
SELECT 'v30 character_parcels cols (expect 2)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'character_parcels' AND COLUMN_NAME IN ('item_unique_id','evolve_amount');

-- ---- v31: inventory_snapshots item_unique_id ------------------------------------
SELECT 'v31 inventory_snapshots.item_unique_id col (expect 1)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'inventory_snapshots' AND COLUMN_NAME = 'item_unique_id';

-- ---- v32: offline-bazaar tables ---------------------------------------------------
SELECT 'v32 item_unique_id_reservations tbl (expect 1)' AS what, COUNT(*) AS value FROM information_schema.TABLES WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'item_unique_id_reservations';
SELECT 'v32 offline_character_sessions tbl (expect 1)' AS what, COUNT(*) AS value FROM information_schema.TABLES WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'offline_character_sessions';
SELECT 'v32 character_offline_transactions tbl (expect 1)' AS what, COUNT(*) AS value FROM information_schema.TABLES WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'character_offline_transactions';

-- ---- v33: character_illusions -------------------------------------------------------
SELECT 'v33 character_illusions tbl (expect 1)' AS what, COUNT(*) AS value FROM information_schema.TABLES WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'character_illusions';

-- ---- v34: npc_types summon_timer_override --------------------------------------------
SELECT 'v34 npc_types.summon_timer_override col (expect 1)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'npc_types' AND COLUMN_NAME = 'summon_timer_override';

-- ---- v39: #Echo_of_Chardok (Veeshan's Peak key / Cipher essence branch) ---------------
SELECT 'v39 chardok echo npc (expect 1)' AS what, COUNT(*) AS value FROM npc_types WHERE id = 103161;
SELECT 'v39 chardok echo spawn2 (expect 3)' AS what, COUNT(*) AS value FROM spawn2 s JOIN spawnentry e ON e.spawngroupID = s.spawngroupID WHERE e.npcID = 103161 AND s.zone = 'chardok';

-- ---- v40: era zones unlocked through Secrets of Faydwer (per-account gates govern) ----
SELECT 'v40 OoW zones open (expect 24)' AS what, COUNT(DISTINCT short_name) AS value FROM zone WHERE expansion = 8 AND min_status = 0;
SELECT 'v40 eras<=14 still locked at 255 (expect 0)' AS what, COUNT(*) AS value FROM zone WHERE expansion <= 14 AND min_status = 255;

-- ---- v41: Echo of Memory -> Triune of Fate rename --------------------------------------
SELECT 'v41 triune currency (expect Triune of Fate)' AS what, `value` AS value FROM db_str WHERE id = 6 AND type = 17 LIMIT 1;
SELECT 'v41 triune drop rule (expect 150)' AS what, rule_value AS value FROM rule_values WHERE rule_name = 'Custom:TriuneOfFateDropChance' LIMIT 1;

-- ---- v42: Drakkin breath weapons (and similar slot-3 resist damage spells) always stack -
-- The rule is a single comma string; assert the Osh'vir lineage (first rank 11112, custom
-- rank-15 clone 50011) is present so a stale/truncated value is caught.
SELECT 'v42 always-stack has breath ids (expect 1)' AS what, (rule_value LIKE '%11112%' AND rule_value LIKE '%50011%') AS value FROM rule_values WHERE rule_name = 'Spells:AlwaysStackSpells' LIMIT 1;

-- ---- v50: alternate-currency rows + client strings self-heal -----------------------------
-- The base release-peq.sql import ships only alternate_currency ids 1 and 6 and no db_str
-- names for ids 4/5 or 30-39; v50 restores the upstream PEQ rows. Both halves are asserted
-- so a half-applied migration (rows without names, or names without rows) is caught. The
-- names are what the client's Alt. Currency tab reads; without them Radiant/Ebon crystals
-- render as "db unknown". Regenerate client files with export_client_files after this lands.
SELECT 'v50 alternate_currency 4/5/10-39 (expect 29)' AS what, COUNT(*) AS value FROM alternate_currency WHERE id IN (4,5,10,11,12,13,14,16,17,18,20,21,22,23,24,25,27,28,29,30,31,32,33,34,35,36,37,38,39);
SELECT 'v50 alt-currency strings 4,5,30-39 x17/18 (expect 24)' AS what, COUNT(*) AS value FROM db_str WHERE type IN (17,18) AND id IN (4,5,30,31,32,33,34,35,36,37,38,39);
SELECT 'v50 radiant crystal name (expect Radiant Crystal)' AS what, `value` AS value FROM db_str WHERE id = 4 AND type = 17 LIMIT 1;
SELECT 'v50 ebon crystal name (expect Ebon Crystal)' AS what, `value` AS value FROM db_str WHERE id = 5 AND type = 17 LIMIT 1;

-- ---- v62/v63: Fabled season ----------------------------------------------------------
-- v62 creates fabled_npcs (content schema); the 472-row roster is seeded by hand from
-- utils/sql/fabled_roster_seed.sql (NOT part of the migration), so the row count is only
-- correct after that seed is applied. v63 creates fabled_season and its single id=1 row.
SELECT 'v62 fabled_npcs tbl (expect 1)' AS what, COUNT(*) AS value FROM information_schema.TABLES WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'fabled_npcs';
SELECT 'v62 fabled roster seeded (expect 472)' AS what, COUNT(*) AS value FROM fabled_npcs;
SELECT 'v62 fabled roster enabled (expect 472)' AS what, COUNT(*) AS value FROM fabled_npcs WHERE enabled = 1;
SELECT 'v63 fabled_season tbl (expect 1)' AS what, COUNT(*) AS value FROM information_schema.TABLES WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'fabled_season';
SELECT 'v63 fabled_season row 1 (expect 1)' AS what, COUNT(*) AS value FROM fabled_season WHERE id = 1;
SELECT 'v63 fabled_season inactive (expect 0)' AS what, active AS value FROM fabled_season WHERE id = 1;

-- ---- v71: bazaar "Echo of X" blessings renamed to "Triune of X" --------------------------
SELECT 'v71 triune blessings renamed (expect 9)' AS what, COUNT(*) AS value FROM spells_new WHERE id IN (17779,36856,43002,43003,43004,43005,43006,43007,43008) AND `name` LIKE 'Triune of %';

-- ---- v80: vault / shared-loot player-schema state ---------------------------------------
-- Table checks use information_schema only, so a missing table reports zero instead of
-- aborting the audit. Exact column-name counts plus type/default/index checks identify a
-- pre-existing table that CREATE TABLE IF NOT EXISTS could not repair.
SELECT 'v80 nms_vault table (expect 1)' AS what, COUNT(*) AS value FROM information_schema.TABLES WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_vault' AND ENGINE = 'InnoDB' AND TABLE_COLLATION LIKE 'utf8mb4%';
SELECT 'v80 nms_vault exact columns (expect 13)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_vault' AND COLUMN_NAME IN ('character_id','page','slot','item_id','charges','augment_one','augment_two','augment_three','augment_four','augment_five','augment_six','attuned','updated_at');
SELECT 'v80 nms_vault total columns (expect 13)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_vault';
SELECT 'v80 nms_vault non-null columns (expect 13)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_vault' AND IS_NULLABLE = 'NO';
SELECT 'v80 nms_vault unsigned columns (expect 10)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_vault' AND COLUMN_NAME IN ('character_id','page','slot','item_id','augment_one','augment_two','augment_three','augment_four','augment_five','augment_six') AND COLUMN_TYPE LIKE '%unsigned%';
SELECT 'v80 nms_vault numeric types (expect 12)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_vault' AND ((COLUMN_NAME IN ('character_id','item_id','augment_one','augment_two','augment_three','augment_four','augment_five','augment_six') AND DATA_TYPE = 'int') OR (COLUMN_NAME IN ('page','attuned') AND DATA_TYPE = 'tinyint') OR (COLUMN_NAME IN ('slot','charges') AND DATA_TYPE = 'smallint'));
SELECT 'v80 nms_vault signed/default item-state columns (expect 8)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_vault' AND ((COLUMN_NAME = 'charges' AND DATA_TYPE = 'smallint' AND COLUMN_TYPE NOT LIKE '%unsigned%' AND COLUMN_DEFAULT = '1') OR (COLUMN_NAME IN ('augment_one','augment_two','augment_three','augment_four','augment_five','augment_six') AND DATA_TYPE = 'int' AND COLUMN_DEFAULT = '0') OR (COLUMN_NAME = 'attuned' AND DATA_TYPE = 'tinyint' AND COLUMN_TYPE NOT LIKE '%unsigned%' AND COLUMN_DEFAULT = '0'));
SELECT 'v80 nms_vault timestamp (expect 1)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_vault' AND COLUMN_NAME = 'updated_at' AND DATA_TYPE = 'timestamp' AND IS_NULLABLE = 'NO' AND COLUMN_DEFAULT LIKE 'current_timestamp%' AND EXTRA LIKE '%on update current_timestamp%';
SELECT 'v80 nms_vault primary key (expect character_id,page,slot)' AS what, GROUP_CONCAT(COLUMN_NAME ORDER BY SEQ_IN_INDEX) AS value FROM information_schema.STATISTICS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_vault' AND INDEX_NAME = 'PRIMARY';
SELECT 'v80 nms_vault idx_char (expect character_id)' AS what, GROUP_CONCAT(COLUMN_NAME ORDER BY SEQ_IN_INDEX) AS value FROM information_schema.STATISTICS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_vault' AND INDEX_NAME = 'idx_char';

SELECT 'v80 nms_vault_bag_items table (expect 1)' AS what, COUNT(*) AS value FROM information_schema.TABLES WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_vault_bag_items' AND ENGINE = 'InnoDB' AND TABLE_COLLATION LIKE 'utf8mb4%';
SELECT 'v80 nms_vault_bag_items exact columns (expect 14)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_vault_bag_items' AND COLUMN_NAME IN ('character_id','page','slot','bag_slot','item_id','charges','augment_one','augment_two','augment_three','augment_four','augment_five','augment_six','attuned','updated_at');
SELECT 'v80 nms_vault_bag_items total columns (expect 14)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_vault_bag_items';
SELECT 'v80 nms_vault_bag_items non-null columns (expect 14)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_vault_bag_items' AND IS_NULLABLE = 'NO';
SELECT 'v80 nms_vault_bag_items unsigned columns (expect 11)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_vault_bag_items' AND COLUMN_NAME IN ('character_id','page','slot','bag_slot','item_id','augment_one','augment_two','augment_three','augment_four','augment_five','augment_six') AND COLUMN_TYPE LIKE '%unsigned%';
SELECT 'v80 nms_vault_bag_items numeric types (expect 13)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_vault_bag_items' AND ((COLUMN_NAME IN ('character_id','item_id','augment_one','augment_two','augment_three','augment_four','augment_five','augment_six') AND DATA_TYPE = 'int') OR (COLUMN_NAME IN ('page','attuned') AND DATA_TYPE = 'tinyint') OR (COLUMN_NAME IN ('slot','bag_slot','charges') AND DATA_TYPE = 'smallint'));
SELECT 'v80 nms_vault_bag_items signed/default item-state columns (expect 8)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_vault_bag_items' AND ((COLUMN_NAME = 'charges' AND DATA_TYPE = 'smallint' AND COLUMN_TYPE NOT LIKE '%unsigned%' AND COLUMN_DEFAULT = '1') OR (COLUMN_NAME IN ('augment_one','augment_two','augment_three','augment_four','augment_five','augment_six') AND DATA_TYPE = 'int' AND COLUMN_DEFAULT = '0') OR (COLUMN_NAME = 'attuned' AND DATA_TYPE = 'tinyint' AND COLUMN_TYPE NOT LIKE '%unsigned%' AND COLUMN_DEFAULT = '0'));
SELECT 'v80 nms_vault_bag_items timestamp (expect 1)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_vault_bag_items' AND COLUMN_NAME = 'updated_at' AND DATA_TYPE = 'timestamp' AND IS_NULLABLE = 'NO' AND COLUMN_DEFAULT LIKE 'current_timestamp%' AND EXTRA LIKE '%on update current_timestamp%';
SELECT 'v80 nms_vault_bag_items primary key (expect character_id,page,slot,bag_slot)' AS what, GROUP_CONCAT(COLUMN_NAME ORDER BY SEQ_IN_INDEX) AS value FROM information_schema.STATISTICS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_vault_bag_items' AND INDEX_NAME = 'PRIMARY';
SELECT 'v80 nms_vault_bag_items idx_bag (expect character_id,page,slot)' AS what, GROUP_CONCAT(COLUMN_NAME ORDER BY SEQ_IN_INDEX) AS value FROM information_schema.STATISTICS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_vault_bag_items' AND INDEX_NAME = 'idx_bag';

SELECT 'v80 nms_loot_rules table (expect 1)' AS what, COUNT(*) AS value FROM information_schema.TABLES WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_loot_rules' AND ENGINE = 'InnoDB' AND TABLE_COLLATION LIKE 'utf8mb4%';
SELECT 'v80 nms_loot_rules exact columns (expect 4)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_loot_rules' AND COLUMN_NAME IN ('character_id','item_id','action','updated_at');
SELECT 'v80 nms_loot_rules total columns (expect 4)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_loot_rules';
SELECT 'v80 nms_loot_rules non-null columns (expect 4)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_loot_rules' AND IS_NULLABLE = 'NO';
SELECT 'v80 nms_loot_rules unsigned state columns (expect 3)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_loot_rules' AND ((COLUMN_NAME IN ('character_id','item_id') AND DATA_TYPE = 'int') OR (COLUMN_NAME = 'action' AND DATA_TYPE = 'tinyint')) AND COLUMN_TYPE LIKE '%unsigned%' AND IS_NULLABLE = 'NO';
SELECT 'v80 nms_loot_rules timestamp (expect 1)' AS what, COUNT(*) AS value FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_loot_rules' AND COLUMN_NAME = 'updated_at' AND DATA_TYPE = 'timestamp' AND IS_NULLABLE = 'NO' AND COLUMN_DEFAULT LIKE 'current_timestamp%' AND EXTRA LIKE '%on update current_timestamp%';
SELECT 'v80 nms_loot_rules primary key (expect character_id,item_id)' AS what, GROUP_CONCAT(COLUMN_NAME ORDER BY SEQ_IN_INDEX) AS value FROM information_schema.STATISTICS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'nms_loot_rules' AND INDEX_NAME = 'PRIMARY';
SELECT 'v80 vault/loot foreign keys (expect 0)' AS what, COUNT(*) AS value FROM information_schema.REFERENTIAL_CONSTRAINTS WHERE CONSTRAINT_SCHEMA = DATABASE() AND TABLE_NAME IN ('nms_vault','nms_vault_bag_items','nms_loot_rules');

SELECT 'v80 NMSLoot ruleset-1 rows (expect 7)' AS what, COUNT(*) AS value FROM rule_values WHERE ruleset_id = 1 AND rule_name IN ('Custom:NMSLootIconOffset','Custom:NMSLootSellPercent','Custom:NMSLootTributePercent','Custom:NMSLootAutoSellUnder','Custom:NMSLootDiscardWorthless','Custom:NMSLootAutoSellMinLevel','Custom:NMSLootRememberDecisions');
SELECT 'v80 NMSLoot ruleset-1 values (informational; operator overrides preserved)' AS what, GROUP_CONCAT(CONCAT(rule_name, '=', rule_value) ORDER BY rule_name SEPARATOR '; ') AS value FROM rule_values WHERE ruleset_id = 1 AND rule_name IN ('Custom:NMSLootIconOffset','Custom:NMSLootSellPercent','Custom:NMSLootTributePercent','Custom:NMSLootAutoSellUnder','Custom:NMSLootDiscardWorthless','Custom:NMSLootAutoSellMinLevel','Custom:NMSLootRememberDecisions');

-- ---- v81: on-demand Vault Speaker -------------------------------------------------------
SELECT 'v81 Vault Speaker 151258 row (expect 1)' AS what, COUNT(*) AS value FROM npc_types WHERE id = 151258;
SELECT 'v81 Vault Speaker attributes (expect 1)' AS what, COUNT(*) AS value FROM npc_types WHERE id = 151258 AND name = 'Vault_Speaker' AND level = 1 AND race = 127 AND class = 1 AND bodytype = 11 AND hp = 100 AND mana = 0 AND gender = 0 AND texture = 0 AND size = 0 AND runspeed = 0 AND npc_faction_id = 0 AND aggroradius = 0 AND assistradius = 0 AND findable = 0 AND trackable = 0 AND show_name = 0 AND untargetable = 1 AND flymode = -1 AND MR = 500 AND CR = 500 AND DR = 500 AND FR = 500 AND PR = 500 AND maxlevel = 1;
SELECT 'v81 Vault Speaker spawnentry references (expect 0)' AS what, COUNT(*) AS value FROM spawnentry WHERE npcID = 151258;
SELECT 'v81 Vault Speaker spawngroup references (expect 0)' AS what, COUNT(DISTINCT g.id) AS value FROM spawngroup g JOIN spawnentry e ON e.spawngroupID = g.id WHERE e.npcID = 151258;
SELECT 'v81 Vault Speaker spawn2 references (expect 0)' AS what, COUNT(*) AS value FROM spawn2 s JOIN spawnentry e ON e.spawngroupID = s.spawngroupID WHERE e.npcID = 151258;
