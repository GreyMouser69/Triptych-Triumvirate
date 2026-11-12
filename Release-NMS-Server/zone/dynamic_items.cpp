#include "dynamic_items.h"

#include <algorithm>
#include <iterator>

#include "../common/classes.h"
#include "../common/database.h"
#include "../common/eqemu_logsys.h"
#include "../common/emu_constants.h"
#include "../common/item_instance.h"
#include "../common/rulesys.h"
#include "../common/spdat.h"
#include "../common/strings.h"
#include "client.h"
#include "dialogue_window.h"
#include "common.h"
#include "zonedb.h"

using namespace EQ;

namespace {
	struct AffixInfo {
		eDynamicAffixID affix;
		const char      *code; // value used in dynamic_affix_pool.affix
		const char      *name; // shown to players
	};

	constexpr AffixInfo affix_info[] = {
		{ AFFIX_HP,                     "hp",                    "HP" },
		{ AFFIX_MANA,                   "mana",                  "Mana" },
		{ AFFIX_ENDURANCE,              "endurance",             "Endurance" },
		{ AFFIX_AC,                     "ac",                    "AC" },
		{ AFFIX_STR,                    "str",                   "Strength" },
		{ AFFIX_STA,                    "sta",                   "Stamina" },
		{ AFFIX_DEX,                    "dex",                   "Dexterity" },
		{ AFFIX_AGI,                    "agi",                   "Agility" },
		{ AFFIX_INT,                    "int",                   "Intelligence" },
		{ AFFIX_WIS,                    "wis",                   "Wisdom" },
		{ AFFIX_CHA,                    "cha",                   "Charisma" },
		{ AFFIX_HEROIC_STR,             "heroic_str",            "Heroic Strength" },
		{ AFFIX_HEROIC_STA,             "heroic_sta",            "Heroic Stamina" },
		{ AFFIX_HEROIC_DEX,             "heroic_dex",            "Heroic Dexterity" },
		{ AFFIX_HEROIC_AGI,             "heroic_agi",            "Heroic Agility" },
		{ AFFIX_HEROIC_INT,             "heroic_int",            "Heroic Intelligence" },
		{ AFFIX_HEROIC_WIS,             "heroic_wis",            "Heroic Wisdom" },
		{ AFFIX_MAGIC_RESIST,           "mr",                    "Magic Resist" },
		{ AFFIX_FIRE_RESIST,            "fr",                    "Fire Resist" },
		{ AFFIX_COLD_RESIST,            "cr",                    "Cold Resist" },
		{ AFFIX_DISEASE_RESIST,         "dr",                    "Disease Resist" },
		{ AFFIX_POISON_RESIST,          "pr",                    "Poison Resist" },
		{ AFFIX_CORRUPTION_RESIST,      "corruption",            "Corruption Resist" },
		{ AFFIX_HEROIC_MAGIC_RESIST,    "heroic_mr",             "Heroic Magic Resist" },
		{ AFFIX_HEROIC_FIRE_RESIST,     "heroic_fr",             "Heroic Fire Resist" },
		{ AFFIX_HEROIC_COLD_RESIST,     "heroic_cr",             "Heroic Cold Resist" },
		{ AFFIX_HEROIC_DISEASE_RESIST,  "heroic_dr",             "Heroic Disease Resist" },
		{ AFFIX_HEROIC_POISON_RESIST,   "heroic_pr",             "Heroic Poison Resist" },
		{ AFFIX_HEROIC_CORRUPTION_RESIST, "heroic_corruption",   "Heroic Corruption Resist" },
		{ AFFIX_SPELL_DAMAGE,           "spelldmg",              "Spell Damage" },
		{ AFFIX_HEAL_AMOUNT,            "healamt",               "Heal Amount" },
		{ AFFIX_ATTACK,                 "attack",                "Attack" },
		{ AFFIX_HP_REGEN,               "hp_regen",              "HP Regen" },
		{ AFFIX_MANA_REGEN,             "mana_regen",            "Mana Regen" },
		{ AFFIX_ENDURANCE_REGEN,        "end_regen",             "Endurance Regen" },
		{ AFFIX_COMBAT_EFFECTS,         "combat_effects",        "Combat Effects" },
		{ AFFIX_SPELL_SHIELD,           "spell_shield",          "Spell Shield" },
		{ AFFIX_SHIELDING,              "shielding",             "Shielding" },
		{ AFFIX_DAMAGE_SHIELD,          "damage_shield",         "Damage Shield" },
		{ AFFIX_DOT_SHIELDING,          "dot_shielding",         "DoT Shielding" },
		{ AFFIX_DS_MITIGATION,          "ds_mitigation",         "Damage Shield Mitigation" },
		{ AFFIX_AVOIDANCE,              "avoidance",             "Avoidance" },
		{ AFFIX_ACCURACY,               "accuracy",              "Accuracy" },
		{ AFFIX_STUN_RESIST,            "stun_resist",           "Stun Resist" },
		{ AFFIX_STRIKE_THROUGH,         "strike_through",        "Strike Through" },
		{ AFFIX_SPELL_CRIT_CHANCE_PCT,  "spell_crit_chance_pct", "Spell Crit Rate %" },
		{ AFFIX_SPELL_CRIT_DMG_PCT,     "spell_crit_dmg_pct",    "Spell Crit Damage %" },
		{ AFFIX_DOT_CRIT_CHANCE_PCT,    "dot_crit_chance_pct",   "DoT Crit Rate %" },
		{ AFFIX_DOT_CRIT_DMG_PCT,       "dot_crit_dmg_pct",      "DoT Crit Damage %" },
		{ AFFIX_HEAL_CRIT_CHANCE_PCT,   "heal_crit_chance_pct",  "Heal Crit Rate %" },
		{ AFFIX_HOT_CRIT_CHANCE_PCT,    "hot_crit_chance_pct",   "HoT Crit Rate %" },
		{ AFFIX_MELEE_CRIT_CHANCE_PCT,  "melee_crit_chance_pct", "Physical Crit Rate %" },
		{ AFFIX_MELEE_CRIT_DMG_PCT,     "melee_crit_dmg_pct",    "Physical Crit Damage %" },
		{ AFFIX_ARCHERY_CRIT_CHANCE_PCT, "archery_crit_chance_pct", "Archery Crit Rate %" },
		{ AFFIX_1H_SLASHING_SKILL_PCT,  "skill_1hs_pct",         "1H Slashing %" },
		{ AFFIX_2H_SLASHING_SKILL_PCT,  "skill_2hs_pct",         "2H Slashing %" },
		{ AFFIX_1H_BLUNT_SKILL_PCT,     "skill_1hb_pct",         "1H Blunt %" },
		{ AFFIX_2H_BLUNT_SKILL_PCT,     "skill_2hb_pct",         "2H Blunt %" },
		{ AFFIX_PIERCING_SKILL_PCT,     "skill_pierce_pct",      "Piercing %" },
		{ AFFIX_HAND_TO_HAND_SKILL_PCT, "skill_h2h_pct",         "Hand to Hand %" },
		{ AFFIX_ARCHERY_SKILL_PCT,      "skill_archery_pct",     "Archery %" },
		{ AFFIX_DEFENSE_SKILL_PCT,      "skill_defense_pct",     "Defense %" },
		{ AFFIX_PARRY_SKILL_PCT,        "skill_parry_pct",       "Parry %" },
		{ AFFIX_RIPOSTE_SKILL_PCT,      "skill_riposte_pct",     "Riposte %" },
		{ AFFIX_DODGE_SKILL_PCT,        "skill_dodge_pct",       "Dodge %" },
		{ AFFIX_BLOCK_SKILL_PCT,        "skill_block_pct",       "Block %" },
		{ AFFIX_FLYING_KICK_SKILL_PCT,  "flying_kick_skill_pct", "Flying Kick %" },
		{ AFFIX_FLYING_KICK_DMG_PCT,    "flying_kick_dmg_pct",   "Flying Kick Damage %" },
		{ AFFIX_BACKSTAB_SKILL_PCT,     "backstab_skill_pct",    "Backstab %" },
		{ AFFIX_BACKSTAB_DMG_PCT,       "backstab_dmg_pct",      "Backstab Damage %" },
		{ AFFIX_FRENZY_SKILL_PCT,       "frenzy_skill_pct",      "Frenzy %" },
		{ AFFIX_FRENZY_DMG_PCT,         "frenzy_dmg_pct",        "Frenzy Damage %" },
		{ AFFIX_BASH_SKILL_PCT,         "bash_skill_pct",        "Bash %" },
		{ AFFIX_BASH_DMG_PCT,           "bash_dmg_pct",          "Bash Damage %" },
		{ AFFIX_FIRE_SPELL_PCT,         "spell_fire_pct",        "Fire Spell Damage %" },
		{ AFFIX_COLD_SPELL_PCT,         "spell_cold_pct",        "Cold Spell Damage %" },
		{ AFFIX_MAGIC_SPELL_PCT,        "spell_magic_pct",       "Magic Spell Damage %" },
		{ AFFIX_POISON_SPELL_PCT,       "spell_poison_pct",      "Poison Spell Damage %" },
		{ AFFIX_DISEASE_SPELL_PCT,      "spell_disease_pct",     "Disease Spell Damage %" },
		{ AFFIX_HEAL_AMOUNT_PCT,        "heal_amount_pct",       "Healing %" },
		{ AFFIX_STRINGED_MOD_PCT,       "bard_stringed_pct",     "Stringed Instruments %" },
		{ AFFIX_WIND_MOD_PCT,           "bard_wind_pct",         "Wind Instruments %" },
		{ AFFIX_BRASS_MOD_PCT,          "bard_brass_pct",        "Brass Instruments %" },
		{ AFFIX_PERCUSSION_MOD_PCT,     "bard_percussion_pct",   "Percussion Instruments %" },
		{ AFFIX_SINGING_MOD_PCT,        "bard_singing_pct",      "Singing %" },
	};

	struct SlotGroup {
		const char *name;
		uint32      mask; // equipment slot bits (1 << slot)
	};

	constexpr SlotGroup slot_groups[] = {
		{ "Charm",        1u << 0 },
		{ "Ear",          (1u << 1) | (1u << 4) },
		{ "Head",         1u << 2 },
		{ "Face",         1u << 3 },
		{ "Neck",         1u << 5 },
		{ "Shoulders",    1u << 6 },
		{ "Arms",         1u << 7 },
		{ "Back",         1u << 8 },
		{ "Wrist",        (1u << 9) | (1u << 10) },
		{ "Range",        1u << 11 },
		{ "Hands",        1u << 12 },
		{ "Weapon",       (1u << 13) | (1u << 14) },
		{ "Finger",       (1u << 15) | (1u << 16) },
		{ "Chest",        1u << 17 },
		{ "Legs",         1u << 18 },
		{ "Feet",         1u << 19 },
		{ "Waist",        1u << 20 },
		{ "Power Source", 1u << 21 },
		{ "Ammo",         1u << 22 },
	};

	// The skill a skill % affix raises, or -1 (used for the item window's one Skill Mod line)
	int AffixSkill(eDynamicAffixID affix)
	{
		using namespace EQ::skills;
		switch (affix) {
			case AFFIX_1H_SLASHING_SKILL_PCT:  return Skill1HSlashing;
			case AFFIX_2H_SLASHING_SKILL_PCT:  return Skill2HSlashing;
			case AFFIX_1H_BLUNT_SKILL_PCT:     return Skill1HBlunt;
			case AFFIX_2H_BLUNT_SKILL_PCT:     return Skill2HBlunt;
			case AFFIX_PIERCING_SKILL_PCT:     return Skill1HPiercing;
			case AFFIX_HAND_TO_HAND_SKILL_PCT: return SkillHandtoHand;
			case AFFIX_ARCHERY_SKILL_PCT:      return SkillArchery;
			case AFFIX_DEFENSE_SKILL_PCT:      return SkillDefense;
			case AFFIX_PARRY_SKILL_PCT:        return SkillParry;
			case AFFIX_RIPOSTE_SKILL_PCT:      return SkillRiposte;
			case AFFIX_DODGE_SKILL_PCT:        return SkillDodge;
			case AFFIX_BLOCK_SKILL_PCT:        return SkillBlock;
			case AFFIX_FLYING_KICK_SKILL_PCT:  return SkillFlyingKick;
			case AFFIX_BACKSTAB_SKILL_PCT:     return SkillBackstab;
			case AFFIX_FRENZY_SKILL_PCT:       return SkillFrenzy;
			case AFFIX_BASH_SKILL_PCT:         return SkillBash;
			default:                           return -1;
		}
	}

	uint64 MixSeed(uint64 x)
	{
		x += 0x9E3779B97F4A7C15ULL;
		x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
		x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
		return x ^ (x >> 31);
	}

	// "Parry %" -> "Parry" (is_pct = true)
	std::string StatLabel(eDynamicAffixID affix, bool &is_pct)
	{
		std::string name = DynamicItemsManager::AffixName(affix);
		is_pct = name.size() > 2 && name.ends_with(" %");
		if (is_pct) {
			name.resize(name.size() - 2);
		}

		return name;
	}

	const AffixInfo *FindAffix(eDynamicAffixID affix)
	{
		for (const auto &a : affix_info) {
			if (a.affix == affix) {
				return &a;
			}
		}

		return nullptr;
	}
}

uint64 DynamicItemsManager::GetSeed(const ItemInstance &inst)
{
	const auto value = inst.GetCustomData(ItemInstance::DYNAMIC_SEED_KEY);
	if (value.empty() || !Strings::IsNumber(value)) {
		return 0;
	}

	return Strings::ToUnsignedBigInt(value);
}

void DynamicItemsManager::SetSeed(ItemInstance &inst, uint64 seed)
{
	inst.SetCustomData(ItemInstance::DYNAMIC_SEED_KEY, std::to_string(seed));
}

std::vector<RolledAffix> DynamicItemsManager::Roll(const ItemInstance &inst) const
{
	const auto item = inst.GetUnscaledItem();
	if (!item) {
		return {};
	}

	const auto def = GetItem(item->ID);
	if (!def) {
		return {};
	}

	const auto pool = GetClassPool(def->class_id);
	if (!pool) {
		return {};
	}

	auto rolled = RollDynamicItem(GetSeed(inst), *def, *pool, item->Slots);
	ApplyDynamicEnchant(rolled, GetEnchant(inst), *def, *pool, item->Slots);
	return rolled;
}

DynamicEnchant DynamicItemsManager::GetEnchant(const ItemInstance &inst)
{
	DynamicEnchant e;
	const auto parts = Strings::Split(inst.GetCustomData(ItemInstance::DYNAMIC_ENCHANT_KEY), '|');
	if (parts.size() != 4) {
		return e;
	}

	e.from  = AffixFromCode(parts[0]);
	e.to    = parts[1].empty() ? AFFIX_NONE : AffixFromCode(parts[1]);
	e.seed  = Strings::IsNumber(parts[2]) ? Strings::ToUnsignedBigInt(parts[2]) : 0;
	e.count = Strings::IsNumber(parts[3]) ? Strings::ToUnsignedInt(parts[3]) : 0;
	return e;
}

void DynamicItemsManager::SetEnchant(ItemInstance &inst, const DynamicEnchant &e)
{
	inst.SetCustomData(
		ItemInstance::DYNAMIC_ENCHANT_KEY,
		fmt::format("{}|{}|{}|{}", AffixCode(e.from), e.to == AFFIX_NONE ? "" : AffixCode(e.to), e.seed, e.count)
	);
}

bool DynamicItemsManager::GetReforgeOffer(const ItemInstance &inst, eDynamicAffixID &from, uint64 &seed)
{
	const auto parts = Strings::Split(inst.GetCustomData(REFORGE_OFFER_KEY), '|');
	if (parts.size() != 2 || !Strings::IsNumber(parts[1])) {
		return false;
	}

	from = AffixFromCode(parts[0]);
	seed = Strings::ToUnsignedBigInt(parts[1]);
	return from != AFFIX_NONE && seed != 0;
}

void DynamicItemsManager::SetReforgeOffer(ItemInstance &inst, eDynamicAffixID from, uint64 seed)
{
	inst.SetCustomData(REFORGE_OFFER_KEY, fmt::format("{}|{}", AffixCode(from), seed));
}

void DynamicItemsManager::ClearReforgeOffer(ItemInstance &inst)
{
	inst.DeleteCustomData(REFORGE_OFFER_KEY);
}

std::vector<RolledAffix> DynamicItemsManager::GetReforgeOptions(const ItemInstance &inst, eDynamicAffixID from, uint64 seed) const
{
	const auto item = inst.GetUnscaledItem();
	const auto def  = item ? GetItem(item->ID) : nullptr;
	const auto pool = def ? GetClassPool(def->class_id) : nullptr;
	if (!pool) {
		return {};
	}

	// The line being reforged is the one rolled as `from` (it may already have been reforged)
	// `taken` collects every other affix on the item so RollReforgeOptions never
	// offers a duplicate of an existing line; rolled affixes are assumed unique.
	const auto enchant = GetEnchant(inst);
	const auto current = enchant.from == from && enchant.to != AFFIX_NONE ? enchant.to : from;

	std::vector<eDynamicAffixID> taken;
	for (const auto &line : Roll(inst)) {
		if (line.affix != current) {
			taken.push_back(line.affix);
		}
	}

	return RollReforgeOptions(seed, taken, *def, *pool, item->Slots, REFORGE_OPTIONS);
}

void DynamicItemsManager::ApplyToInstance(ItemInstance &inst)
{
	const auto base = inst.GetUnscaledItem();
	if (!base) {
		return;
	}

	const auto rolled = Instance()->Roll(inst);
	if (rolled.empty()) {
		inst.ClearDynamicItemData();
		return;
	}

	// Regular stats and resists are int8 on the item; keep big multipliers from wrapping negative
	auto add8 = [](int8 &field, int32 value) {
		field = static_cast<int8>(std::clamp<int32>(field + value, INT8_MIN, INT8_MAX));
	};

	ItemData data = *base;
	for (const auto &line : rolled) {
		switch (line.affix) {
			case AFFIX_STR:        add8(data.AStr, line.value); break;
			case AFFIX_STA:        add8(data.ASta, line.value); break;
			case AFFIX_DEX:        add8(data.ADex, line.value); break;
			case AFFIX_AGI:        add8(data.AAgi, line.value); break;
			case AFFIX_INT:        add8(data.AInt, line.value); break;
			case AFFIX_WIS:        add8(data.AWis, line.value); break;
			case AFFIX_CHA:        add8(data.ACha, line.value); break;
			case AFFIX_MAGIC_RESIST:   add8(data.MR, line.value); break;
			case AFFIX_FIRE_RESIST:    add8(data.FR, line.value); break;
			case AFFIX_COLD_RESIST:    add8(data.CR, line.value); break;
			case AFFIX_DISEASE_RESIST: add8(data.DR, line.value); break;
			case AFFIX_POISON_RESIST:  add8(data.PR, line.value); break;
			case AFFIX_CORRUPTION_RESIST:         data.SVCorruption += line.value; break;
			case AFFIX_HEROIC_MAGIC_RESIST:       data.HeroicMR += line.value; break;
			case AFFIX_HEROIC_FIRE_RESIST:        data.HeroicFR += line.value; break;
			case AFFIX_HEROIC_COLD_RESIST:        data.HeroicCR += line.value; break;
			case AFFIX_HEROIC_DISEASE_RESIST:     data.HeroicDR += line.value; break;
			case AFFIX_HEROIC_POISON_RESIST:      data.HeroicPR += line.value; break;
			case AFFIX_HEROIC_CORRUPTION_RESIST:  data.HeroicSVCorrup += line.value; break;
			case AFFIX_SPELL_DAMAGE:     data.SpellDmg += line.value; break;
			case AFFIX_HEAL_AMOUNT:      data.HealAmt += line.value; break;
			case AFFIX_ATTACK:           data.Attack += line.value; break;
			case AFFIX_HP_REGEN:         data.Regen += line.value; break;
			case AFFIX_MANA_REGEN:       data.ManaRegen += line.value; break;
			case AFFIX_ENDURANCE_REGEN:  data.EnduranceRegen += line.value; break;
			case AFFIX_COMBAT_EFFECTS:   add8(data.CombatEffects, line.value); break;
			case AFFIX_SPELL_SHIELD:     add8(data.SpellShield, line.value); break;
			case AFFIX_SHIELDING:        add8(data.Shielding, line.value); break;
			case AFFIX_DAMAGE_SHIELD:    data.DamageShield += line.value; break;
			case AFFIX_DOT_SHIELDING:    data.DotShielding += line.value; break;
			case AFFIX_DS_MITIGATION:    data.DSMitigation += static_cast<uint32>(std::max(line.value, 0)); break;
			case AFFIX_AVOIDANCE:        add8(data.Avoidance, line.value); break;
			case AFFIX_ACCURACY:         add8(data.Accuracy, line.value); break;
			case AFFIX_STUN_RESIST:      add8(data.StunResist, line.value); break;
			case AFFIX_STRIKE_THROUGH:   add8(data.StrikeThrough, line.value); break;
			case AFFIX_HP:         data.HP += line.value; break;
			case AFFIX_MANA:       data.Mana += line.value; break;
			case AFFIX_ENDURANCE:  data.Endur += line.value; break;
			case AFFIX_AC:         data.AC += line.value; break;
			case AFFIX_HEROIC_STR: data.HeroicStr += line.value; break;
			case AFFIX_HEROIC_STA: data.HeroicSta += line.value; break;
			case AFFIX_HEROIC_DEX: data.HeroicDex += line.value; break;
			case AFFIX_HEROIC_AGI: data.HeroicAgi += line.value; break;
			case AFFIX_HEROIC_INT: data.HeroicInt += line.value; break;
			case AFFIX_HEROIC_WIS: data.HeroicWis += line.value; break;
			default: break;
		}
	}

	// Diablo-style name from the best random roll (relative to its range), e.g. "... of the Assassin"
	const RolledAffix *top = nullptr;
	for (const auto &line : rolled) {
		if (!line.guaranteed && (!top || line.position > top->position)) {
			top = &line;
		}
	}

	if (top) {
		const auto suffix = Instance()->GetSuffix(top->affix);
		if (!suffix.empty()) {
			strn0cpy(data.Name, fmt::format("{} {}", base->Name, suffix).c_str(), sizeof(data.Name));
		}
	}

	// The item window has one Skill Mod line: show the biggest skill %. Display only; AddItemBonuses
	// skips it for dynamic items because ApplySkillBonuses already counts every skill affix.
	for (const auto &line : rolled) {
		const int skill = AffixSkill(line.affix);
		if (skill >= 0 && line.value > data.SkillModValue) {
			data.SkillModType  = static_cast<uint32>(skill);
			data.SkillModValue = line.value;
			data.SkillModMax   = 0;
		}
	}

	inst.SetDynamicItemData(data);

	// Own item ID per copy so the client's item window doesn't reuse another copy's stats
	if (RuleB(Custom, DynamicItemDisplayIds)) {
		// A reforge changes the stats, so it gets a new display ID too
		const uint64 seed       = GetSeed(inst);
		const auto   enchant    = inst.GetCustomData(ItemInstance::DYNAMIC_ENCHANT_KEY);
		const uint64 mixed      = enchant.empty() ? seed : seed ^ MixSeed(std::hash<std::string>{}(enchant));
		const uint32 display_id = DISPLAY_ID_BASE + static_cast<uint32>(MixSeed(mixed) % (0xFFFFFFF - DISPLAY_ID_BASE));
		inst.SetDisplayID(display_id);
		Instance()->m_display_ids[display_id] = DisplayIdEntry{ base->ID, seed, enchant };
	} else {
		inst.SetDisplayID(0);
	}
}

bool DynamicItemsManager::ResolveDisplayId(uint32 display_id, uint32 &item_id, uint64 &seed, std::string &enchant) const
{
	const auto it = m_display_ids.find(display_id);
	if (it == m_display_ids.end()) {
		return false;
	}

	item_id = it->second.item_id;
	seed    = it->second.seed;
	enchant = it->second.enchant;
	return true;
}

uint32 DynamicItemsManager::ResolveItemId(uint32 id) const
{
	const auto it = m_display_ids.find(id);
	return it != m_display_ids.end() ? it->second.item_id : id;
}

std::string DynamicItemsManager::GetSuffix(eDynamicAffixID affix) const
{
	const auto it = m_suffixes.find(AffixCode(affix));
	return it != m_suffixes.end() ? it->second : std::string();
}

int DynamicItemsManager::SlotGroupForSlots(uint32 item_slots)
{
	for (int i = 0; i < SlotGroupCount(); ++i) {
		if (slot_groups[i].mask & item_slots) {
			return i;
		}
	}

	return -1;
}

int DynamicItemsManager::SlotGroupForEquipSlot(int16 slot_id)
{
	if (slot_id < invslot::EQUIPMENT_BEGIN || slot_id > invslot::EQUIPMENT_END) {
		return -1;
	}

	return SlotGroupForSlots(1u << slot_id);
}

int DynamicItemsManager::SlotGroupFromName(const std::string &name)
{
	auto key = Strings::ToLower(name);
	if (key == "primary" || key == "secondary" || key == "weapons") {
		key = "weapon";
	}

	for (int i = 0; i < SlotGroupCount(); ++i) {
		const auto group = Strings::ToLower(slot_groups[i].name);
		if (key == group || key == group + "s" || key == Strings::Replace(group, " ", "")) {
			return i;
		}
	}

	return -1;
}

const char *DynamicItemsManager::SlotGroupName(int group)
{
	return group >= 0 && group < SlotGroupCount() ? slot_groups[group].name : "Other";
}

int DynamicItemsManager::SlotGroupCount()
{
	return static_cast<int>(std::size(slot_groups));
}

std::string DynamicItemsManager::FormatLine(const RolledAffix &line)
{
	bool       is_pct = false;
	const auto name   = StatLabel(line.affix, is_pct);
	return fmt::format("{}{:+}{} {}", line.guaranteed ? "" : "* ", line.value, is_pct ? "%" : "", name);
}

DynamicItemComparison DynamicItemsManager::Compare(
	const std::vector<RolledAffix> &base,
	const std::vector<RolledAffix> &other,
	bool color
)
{
	DynamicItemComparison r;

	auto paint = [&](const char *c, const std::string &s) {
		return color ? DialogueWindow::ColorMessage(c, s) : s;
	};

	int best_gain = 0;
	for (const auto &line : other) {
		bool       is_pct = false;
		const auto name   = StatLabel(line.affix, is_pct);
		const auto unit   = is_pct ? "%" : "";

		const auto match = std::find_if(
			base.begin(),
			base.end(),
			[&](const RolledAffix &b) { return b.affix == line.affix; }
		);

		int         gain = 0;
		std::string diff;
		if (match == base.end()) {
			gain = line.value;
			r.better++;
			diff = paint("green", " (new)");
			r.changes.push_back(fmt::format("{} {:+}{} (new)", name, line.value, unit));
		} else if (line.value != match->value) {
			gain = line.value - match->value;
			(gain > 0 ? r.better : r.worse)++;
			diff = paint(gain > 0 ? "green" : "red", fmt::format(" ({:+})", gain));
			r.changes.push_back(fmt::format("{} {:+}{}", name, gain, unit));
		}

		r.lines.push_back(FormatLine(line) + diff);

		if (!line.guaranteed && gain > best_gain) {
			best_gain  = gain;
			r.standout = fmt::format("{} {:+}{}", name, gain, unit);
		}
	}

	for (const auto &line : base) {
		const bool kept = std::any_of(
			other.begin(),
			other.end(),
			[&](const RolledAffix &o) { return o.affix == line.affix; }
		);
		if (kept) {
			continue;
		}

		bool       is_pct = false;
		const auto name   = StatLabel(line.affix, is_pct);
		r.worse++;
		r.lines.push_back(paint("red", fmt::format("{} (lost)", name)));
		r.changes.push_back(fmt::format("{} lost", name));
	}

	return r;
}

void DynamicItemsManager::ApplyAffix(eDynamicAffixID affix, int32 value, DynamicItemBonuses *d)
{
	switch (affix) {
		case AFFIX_1H_SLASHING_SKILL_PCT:  d->OneHandSlashingSkillPct += value; break;
		case AFFIX_2H_SLASHING_SKILL_PCT:  d->TwoHandSlashingSkillPct += value; break;
		case AFFIX_1H_BLUNT_SKILL_PCT:     d->OneHandBluntSkillPct += value; break;
		case AFFIX_2H_BLUNT_SKILL_PCT:     d->TwoHandBluntSkillPct += value; break;
		case AFFIX_PIERCING_SKILL_PCT:     d->PiercingSkillPct += value; break;
		case AFFIX_HAND_TO_HAND_SKILL_PCT: d->HandToHandSkillPct += value; break;
		case AFFIX_ARCHERY_SKILL_PCT:      d->ArcherySkillPct += value; break;
		case AFFIX_DEFENSE_SKILL_PCT:      d->DefenseSkillPct += value; break;
		case AFFIX_PARRY_SKILL_PCT:        d->ParrySkillPct += value; break;
		case AFFIX_RIPOSTE_SKILL_PCT:      d->RiposteSkillPct += value; break;
		case AFFIX_DODGE_SKILL_PCT:        d->DodgeSkillPct += value; break;
		case AFFIX_BLOCK_SKILL_PCT:        d->BlockSkillPct += value; break;
		case AFFIX_FLYING_KICK_SKILL_PCT:  d->FlyingKickSkillPct += value; break;
		case AFFIX_FLYING_KICK_DMG_PCT:    d->FlyingKickDmgPct += value; break;
		case AFFIX_BACKSTAB_SKILL_PCT:     d->BackstabSkillPct += value; break;
		case AFFIX_BACKSTAB_DMG_PCT:       d->BackstabDmgPct += value; break;
		case AFFIX_FRENZY_SKILL_PCT:       d->FrenzySkillPct += value; break;
		case AFFIX_FRENZY_DMG_PCT:         d->FrenzyDmgPct += value; break;
		case AFFIX_BASH_SKILL_PCT:         d->BashSkillPct += value; break;
		case AFFIX_BASH_DMG_PCT:           d->BashDmgPct += value; break;
		case AFFIX_FIRE_SPELL_PCT:         d->FireDamagePct += value; break;
		case AFFIX_COLD_SPELL_PCT:         d->ColdDamagePct += value; break;
		case AFFIX_MAGIC_SPELL_PCT:        d->MagicDamagePct += value; break;
		case AFFIX_POISON_SPELL_PCT:       d->PoisonDamagePct += value; break;
		case AFFIX_DISEASE_SPELL_PCT:      d->DiseaseDamagePct += value; break;
		case AFFIX_HEAL_AMOUNT_PCT:        d->HealAmountPct += value; break;
		case AFFIX_STRINGED_MOD_PCT:       d->StringedModPct += value; break;
		case AFFIX_WIND_MOD_PCT:           d->WindModPct += value; break;
		case AFFIX_BRASS_MOD_PCT:          d->BrassModPct += value; break;
		case AFFIX_PERCUSSION_MOD_PCT:     d->PercussionModPct += value; break;
		case AFFIX_SINGING_MOD_PCT:        d->SingingModPct += value; break;
		case AFFIX_SPELL_CRIT_CHANCE_PCT:  d->SpellCritChancePct += value; break;
		case AFFIX_SPELL_CRIT_DMG_PCT:     d->SpellCritDmgPct += value; break;
		case AFFIX_DOT_CRIT_CHANCE_PCT:    d->DotCritChancePct += value; break;
		case AFFIX_DOT_CRIT_DMG_PCT:       d->DotCritDmgPct += value; break;
		case AFFIX_HEAL_CRIT_CHANCE_PCT:   d->HealCritChancePct += value; break;
		case AFFIX_HOT_CRIT_CHANCE_PCT:    d->HotCritChancePct += value; break;
		case AFFIX_MELEE_CRIT_CHANCE_PCT:  d->MeleeCritChancePct += value; break;
		case AFFIX_MELEE_CRIT_DMG_PCT:     d->MeleeCritDmgPct += value; break;
		case AFFIX_ARCHERY_CRIT_CHANCE_PCT: d->ArcheryCritChancePct += value; break;
		default: break; // flat stats live on the item itself
	}
}

void DynamicItemsManager::ApplyCharacterAffix(eDynamicAffixID affix, int32 value, StatBonuses *b, DynamicItemBonuses *d)
{
	// Same fields AddItemBonuses fills from an item's stats; heroics also count toward the regular
	// stat and raise its cap, as they do on items
	switch (affix) {
		case AFFIX_HP:        b->HP += value; break;
		case AFFIX_MANA:      b->Mana += value; break;
		case AFFIX_ENDURANCE: b->Endurance += value; break;
		case AFFIX_AC:        b->AC += value; break;
		case AFFIX_STR: b->STR += value; break;
		case AFFIX_STA: b->STA += value; break;
		case AFFIX_DEX: b->DEX += value; break;
		case AFFIX_AGI: b->AGI += value; break;
		case AFFIX_INT: b->INT += value; break;
		case AFFIX_WIS: b->WIS += value; break;
		case AFFIX_CHA: b->CHA += value; break;
		case AFFIX_HEROIC_STR: b->STR += value; b->HeroicSTR += value; b->STRCapMod += value; break;
		case AFFIX_HEROIC_STA: b->STA += value; b->HeroicSTA += value; b->STACapMod += value; break;
		case AFFIX_HEROIC_DEX: b->DEX += value; b->HeroicDEX += value; b->DEXCapMod += value; break;
		case AFFIX_HEROIC_AGI: b->AGI += value; b->HeroicAGI += value; b->AGICapMod += value; break;
		case AFFIX_HEROIC_INT: b->INT += value; b->HeroicINT += value; b->INTCapMod += value; break;
		case AFFIX_HEROIC_WIS: b->WIS += value; b->HeroicWIS += value; b->WISCapMod += value; break;
		case AFFIX_MAGIC_RESIST:      b->MR += value; break;
		case AFFIX_FIRE_RESIST:       b->FR += value; break;
		case AFFIX_COLD_RESIST:       b->CR += value; break;
		case AFFIX_DISEASE_RESIST:    b->DR += value; break;
		case AFFIX_POISON_RESIST:     b->PR += value; break;
		case AFFIX_CORRUPTION_RESIST: b->Corrup += value; break;
		case AFFIX_HEROIC_MAGIC_RESIST:      b->MR += value; b->HeroicMR += value; b->MRCapMod += value; break;
		case AFFIX_HEROIC_FIRE_RESIST:       b->FR += value; b->HeroicFR += value; b->FRCapMod += value; break;
		case AFFIX_HEROIC_COLD_RESIST:       b->CR += value; b->HeroicCR += value; b->CRCapMod += value; break;
		case AFFIX_HEROIC_DISEASE_RESIST:    b->DR += value; b->HeroicDR += value; b->DRCapMod += value; break;
		case AFFIX_HEROIC_POISON_RESIST:     b->PR += value; b->HeroicPR += value; b->PRCapMod += value; break;
		case AFFIX_HEROIC_CORRUPTION_RESIST: b->Corrup += value; b->HeroicCorrup += value; b->CorrupCapMod += value; break;
		case AFFIX_SPELL_DAMAGE:    b->SpellDmg += value; break;
		case AFFIX_HEAL_AMOUNT:     b->HealAmt += value; break;
		case AFFIX_ATTACK:          b->ATK += value; break;
		case AFFIX_HP_REGEN:        b->HPRegen += value; break;
		case AFFIX_MANA_REGEN:      b->ManaRegen += value; break;
		case AFFIX_ENDURANCE_REGEN: b->EnduranceRegen += value; break;
		case AFFIX_COMBAT_EFFECTS:  b->ProcChance += value; break;
		case AFFIX_SPELL_SHIELD:    b->SpellShield += value; break;
		case AFFIX_SHIELDING:       b->MeleeMitigation += value; break;
		case AFFIX_DAMAGE_SHIELD:   b->DamageShield += value; break;
		case AFFIX_DOT_SHIELDING:   b->DoTShielding += value; break;
		case AFFIX_DS_MITIGATION:   b->DSMitigation += value; break;
		case AFFIX_AVOIDANCE:       b->AvoidMeleeChance += value; break;
		case AFFIX_ACCURACY:        b->HitChance += value; break;
		case AFFIX_STUN_RESIST:     b->StunResist += value; break;
		case AFFIX_STRIKE_THROUGH:  b->StrikeThrough += value; break;
		default: ApplyAffix(affix, value, d); break;
	}
}

void DynamicItemsManager::ApplySkillBonuses(const DynamicItemBonuses &d, StatBonuses *b)
{
	using namespace EQ::skills;

	b->skillmod[Skill1HSlashing]  += d.OneHandSlashingSkillPct;
	b->skillmod[Skill2HSlashing]  += d.TwoHandSlashingSkillPct;
	b->skillmod[Skill1HBlunt]     += d.OneHandBluntSkillPct;
	b->skillmod[Skill2HBlunt]     += d.TwoHandBluntSkillPct;
	b->skillmod[Skill1HPiercing]  += d.PiercingSkillPct;
	b->skillmod[Skill2HPiercing]  += d.PiercingSkillPct;
	b->skillmod[SkillHandtoHand]  += d.HandToHandSkillPct;
	b->skillmod[SkillArchery]     += d.ArcherySkillPct;

	b->skillmod[SkillDefense]     += d.DefenseSkillPct;
	b->skillmod[SkillParry]       += d.ParrySkillPct;
	b->skillmod[SkillRiposte]     += d.RiposteSkillPct;
	b->skillmod[SkillDodge]       += d.DodgeSkillPct;
	b->skillmod[SkillBlock]       += d.BlockSkillPct;

	b->skillmod[SkillFlyingKick]  += d.FlyingKickSkillPct;
	b->skillmod[SkillBackstab]    += d.BackstabSkillPct;
	b->skillmod[SkillFrenzy]      += d.FrenzySkillPct;
	b->skillmod[SkillBash]        += d.BashSkillPct;

	b->DamageModifier[SkillFlyingKick] += d.FlyingKickDmgPct;
	b->DamageModifier[SkillBackstab]   += d.BackstabDmgPct;
	b->DamageModifier[SkillFrenzy]     += d.FrenzyDmgPct;
	b->DamageModifier[SkillBash]       += d.BashDmgPct;

	// Critical hits; HIGHEST_SKILL + 1 is the "all skills" slot
	b->CriticalSpellChance                       += d.SpellCritChancePct;
	b->SpellCritDmgIncrease                      += d.SpellCritDmgPct;
	b->CriticalDoTChance                         += d.DotCritChancePct;
	b->DotCritDmgIncrease                        += d.DotCritDmgPct;
	b->CriticalHealChance                        += d.HealCritChancePct;
	b->CriticalHealOverTime                      += d.HotCritChancePct;
	b->CriticalHitChance[HIGHEST_SKILL + 1]      += d.MeleeCritChancePct;
	b->CritDmgMod[HIGHEST_SKILL + 1]             += d.MeleeCritDmgPct;
	b->CriticalHitChance[SkillArchery]           += d.ArcheryCritChancePct;
}

void DynamicItemsManager::CapBonuses(DynamicItemBonuses &d)
{
	const int32 pct_cap         = RuleI(Custom, DynamicItemPctCap);
	const int32 crit_chance_cap = RuleI(Custom, DynamicItemCritChancePctCap);

	auto cap = [](int32 &value, int32 limit) {
		if (limit > 0 && value > limit) {
			value = limit;
		}
	};

	for (int32 *v : {
		&d.OneHandSlashingSkillPct, &d.TwoHandSlashingSkillPct, &d.OneHandBluntSkillPct, &d.TwoHandBluntSkillPct,
		&d.PiercingSkillPct, &d.HandToHandSkillPct, &d.ArcherySkillPct,
		&d.DefenseSkillPct, &d.ParrySkillPct, &d.RiposteSkillPct, &d.DodgeSkillPct, &d.BlockSkillPct,
		&d.FlyingKickSkillPct, &d.FlyingKickDmgPct, &d.BackstabSkillPct, &d.BackstabDmgPct,
		&d.FrenzySkillPct, &d.FrenzyDmgPct, &d.BashSkillPct, &d.BashDmgPct,
		&d.FireDamagePct, &d.ColdDamagePct, &d.MagicDamagePct, &d.PoisonDamagePct, &d.DiseaseDamagePct,
		&d.HealAmountPct,
		&d.StringedModPct, &d.WindModPct, &d.BrassModPct, &d.PercussionModPct, &d.SingingModPct,
		&d.SpellCritDmgPct, &d.DotCritDmgPct, &d.MeleeCritDmgPct,
	}) {
		cap(*v, pct_cap);
	}

	for (int32 *v : {
		&d.SpellCritChancePct, &d.DotCritChancePct, &d.HealCritChancePct, &d.HotCritChancePct,
		&d.MeleeCritChancePct, &d.ArcheryCritChancePct,
	}) {
		cap(*v, crit_chance_cap);
	}
}

int32 DynamicItemsManager::GetSpellDamagePct(const DynamicItemBonuses &d, uint16 spell_id)
{
	if (!IsValidSpell(spell_id)) {
		return 0;
	}

	switch (::spells[spell_id].resist_type) {
		case RESIST_FIRE:    return d.FireDamagePct;
		case RESIST_COLD:    return d.ColdDamagePct;
		case RESIST_MAGIC:   return d.MagicDamagePct;
		case RESIST_POISON:  return d.PoisonDamagePct;
		case RESIST_DISEASE: return d.DiseaseDamagePct;
		default:             return 0;
	}
}

int32 DynamicItemsManager::GetSongModPct(const DynamicItemBonuses &d, int skill)
{
	switch (skill) {
		case EQ::skills::SkillStringedInstruments:   return d.StringedModPct;
		case EQ::skills::SkillWindInstruments:       return d.WindModPct;
		case EQ::skills::SkillBrassInstruments:      return d.BrassModPct;
		case EQ::skills::SkillPercussionInstruments: return d.PercussionModPct;
		case EQ::skills::SkillSinging:               return d.SingingModPct;
		default:                                     return 0;
	}
}

std::vector<std::string> DynamicItemsManager::Describe(const ItemInstance &inst) const
{
	std::vector<std::string> lines;

	const auto item = inst.GetUnscaledItem();
	const auto def  = item ? GetItem(item->ID) : nullptr;
	if (!def) {
		return lines;
	}

	lines.emplace_back(
		fmt::format("{} (Tier {} {})", inst.GetItem()->Name, def->tier, GetClassIDName(def->class_id))
	);

	for (const auto &line : Roll(inst)) {
		lines.emplace_back(FormatLine(line));
	}

	return lines;
}

void DynamicItemsManager::SendSummary(Client *c, const ItemInstance &inst) const
{
	const auto lines = Describe(inst);
	if (!c || lines.empty()) {
		return;
	}

	// Message() is printf-style and these lines contain '%', so they must go in as arguments
	c->Message(Chat::Yellow, "%s", fmt::format("You receive {}:", lines.front()).c_str());

	std::vector<std::string> stats(lines.begin() + 1, lines.end());
	c->Message(Chat::White, "%s", Strings::Join(stats, ", ").c_str());

	// Compare with the dynamic item worn in the same slot, if there is one
	const auto group = SlotGroupForSlots(inst.GetUnscaledItem()->Slots);
	for (int16 slot_id = invslot::EQUIPMENT_BEGIN; slot_id <= invslot::EQUIPMENT_END; ++slot_id) {
		const auto worn = c->GetInv().GetItem(slot_id);
		if (!worn || worn == &inst || !IsDynamicItem(worn->GetID()) || SlotGroupForEquipSlot(slot_id) != group) {
			continue;
		}

		const auto cmp  = Compare(Roll(*worn), Roll(inst), false);
		const auto text = cmp.changes.empty()
			? fmt::format("Same as your equipped {}.", worn->GetItem()->Name)
			: fmt::format("vs equipped {}: {}", worn->GetItem()->Name, Strings::Join(cmp.changes, ", "));
		c->Message(Chat::White, "%s", text.c_str());
		break;
	}
}

int DynamicItemsManager::RefreshClient(Client *c) const
{
	if (!c) {
		return 0;
	}

	int updated = 0;

	auto refresh = [&](int16 slot_id, bool send) {
		auto inst = c->GetInv().GetItem(slot_id);
		if (!inst || !IsDynamicItem(inst->GetID())) {
			return;
		}

		ApplyToInstance(*inst);
		if (send) {
			c->SendItemPacket(slot_id, inst, ItemPacketTrade);
		}
		updated++;
	};

	// Items the client is holding are resent now; bank contents refresh on the next zone
	for (int16 slot_id = invslot::POSSESSIONS_BEGIN; slot_id <= invslot::POSSESSIONS_END; ++slot_id) {
		refresh(slot_id, slot_id != invslot::slotCursor);
	}

	for (int16 slot_id = invbag::GENERAL_BAGS_BEGIN; slot_id <= invbag::GENERAL_BAGS_END; ++slot_id) {
		refresh(slot_id, true);
	}

	for (int16 slot_id = invbag::CURSOR_BAG_BEGIN; slot_id <= invbag::CURSOR_BAG_END; ++slot_id) {
		refresh(slot_id, false);
	}

	for (int16 slot_id = invslot::BANK_BEGIN; slot_id <= invslot::BANK_END; ++slot_id) {
		refresh(slot_id, false);
	}

	for (int16 slot_id = invbag::BANK_BAGS_BEGIN; slot_id <= invbag::BANK_BAGS_END; ++slot_id) {
		refresh(slot_id, false);
	}

	for (int16 slot_id = invslot::SHARED_BANK_BEGIN; slot_id <= invslot::SHARED_BANK_END; ++slot_id) {
		refresh(slot_id, false);
	}

	for (int16 slot_id = invbag::SHARED_BANK_BAGS_BEGIN; slot_id <= invbag::SHARED_BANK_BAGS_END; ++slot_id) {
		refresh(slot_id, false);
	}

	if (updated) {
		c->CalcBonuses();
	}

	return updated;
}

uint32 DynamicItemsManager::GetSellRefund(uint32 full_cost)
{
	const auto pct = EQ::Clamp(RuleI(Merchant, DynamicItemSellRefundPct), 0, 100);
	return static_cast<uint32>(static_cast<uint64>(full_cost) * pct / 100);
}

eDynamicAffixID DynamicItemsManager::AffixFromCode(const std::string &code)
{
	const auto lower = Strings::ToLower(code);
	for (const auto &a : affix_info) {
		if (lower == a.code) {
			return a.affix;
		}
	}

	return AFFIX_NONE;
}

const char *DynamicItemsManager::AffixCode(eDynamicAffixID affix)
{
	const auto a = FindAffix(affix);
	return a ? a->code : "none";
}

const char *DynamicItemsManager::AffixName(eDynamicAffixID affix)
{
	const auto a = FindAffix(affix);
	return a ? a->name : "Unknown";
}

bool DynamicItemsManager::Load()
{
	if (!m_content_db) {
		LogError("DynamicItemsManager has no content database");
		return false;
	}

	m_items.clear();
	m_class_pools.clear();

	auto results = m_content_db->QueryDatabase(
		"SELECT `item_id`, `class_id`, `tier`, `min_affixes`, `max_affixes`, `stat_multiplier` FROM `dynamic_items`"
	);
	if (!results.Success()) {
		LogError("Failed to load dynamic_items");
		return false;
	}

	for (auto row : results) {
		DynamicItemDef e;
		e.item_id         = Strings::ToUnsignedInt(row[0]);
		e.class_id        = static_cast<uint8>(Strings::ToUnsignedInt(row[1]));
		e.tier            = static_cast<uint8>(Strings::ToUnsignedInt(row[2]));
		e.min_affixes     = static_cast<uint8>(Strings::ToUnsignedInt(row[3]));
		e.max_affixes     = static_cast<uint8>(Strings::ToUnsignedInt(row[4]));
		e.stat_multiplier = Strings::ToFloat(row[5]);

		if (e.class_id < Class::Warrior || e.class_id > Class::Berserker) {
			LogError("dynamic_items item_id [{}] has invalid class_id [{}], skipping", e.item_id, e.class_id);
			continue;
		}

		if (e.max_affixes < e.min_affixes) {
			LogWarning(
				"dynamic_items item_id [{}] has max_affixes [{}] below min_affixes [{}], using min",
				e.item_id,
				e.max_affixes,
				e.min_affixes
			);
			e.max_affixes = e.min_affixes;
		}

		m_items[e.item_id] = e;
	}

	results = m_content_db->QueryDatabase(
		"SELECT `class_id`, `affix`, `guaranteed`, `weight`, `min_value`, `max_value`, `slot_mask` "
		"FROM `dynamic_affix_pool` ORDER BY `class_id`, `affix`"
	);
	if (!results.Success()) {
		LogError("Failed to load dynamic_affix_pool");
		return false;
	}

	uint32 line_count = 0;
	for (auto row : results) {
		const auto class_id = static_cast<uint8>(Strings::ToUnsignedInt(row[0]));

		DynamicAffixDef e;
		e.affix      = AffixFromCode(row[1]);
		e.guaranteed = Strings::ToUnsignedInt(row[2]) != 0;
		e.weight     = Strings::ToUnsignedInt(row[3]);
		e.min_value  = Strings::ToInt(row[4]);
		e.max_value  = Strings::ToInt(row[5]);
		e.slot_mask  = Strings::ToUnsignedInt(row[6]);

		if (e.affix == AFFIX_NONE) {
			LogError("dynamic_affix_pool class_id [{}] has unknown affix [{}], skipping", class_id, row[1]);
			continue;
		}

		if (e.max_value < e.min_value) {
			std::swap(e.min_value, e.max_value);
		}

		m_class_pools[class_id].push_back(e);
		line_count++;
	}

	m_suffixes.clear();
	results = m_content_db->QueryDatabase("SELECT `affix`, `suffix` FROM `dynamic_affix_suffixes`");
	if (results.Success()) {
		for (auto row : results) {
			if (AffixFromCode(row[0]) == AFFIX_NONE) {
				LogWarning("dynamic_affix_suffixes has unknown affix [{}], skipping", row[0]);
				continue;
			}

			m_suffixes[Strings::ToLower(row[0])] = row[1];
		}
	} else {
		LogWarning("Failed to load dynamic_affix_suffixes; items will have no name suffixes");
	}

	LogInfo(
		"Loaded [{}] dynamic items, [{}] affix lines across [{}] classes and [{}] name suffixes",
		m_items.size(),
		line_count,
		m_class_pools.size(),
		m_suffixes.size()
	);

	return true;
}

const DynamicItemDef *DynamicItemsManager::GetItem(uint32 item_id) const
{
	auto it = m_items.find(item_id);
	return it != m_items.end() ? &it->second : nullptr;
}

const std::vector<DynamicAffixDef> *DynamicItemsManager::GetClassPool(uint8 class_id) const
{
	auto it = m_class_pools.find(class_id);
	return it != m_class_pools.end() ? &it->second : nullptr;
}
