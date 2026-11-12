#ifndef DYNAMIC_ITEM_STRUCTS_H
#define DYNAMIC_ITEM_STRUCTS_H

#include <stdint.h>
#include <vector>
#include <string>

namespace EQ {

// Enums for Affix Types
enum eDynamicAffixID {
	AFFIX_NONE = 0,

	// Core & Heroic Stats
	AFFIX_HP,
	AFFIX_MANA,
	AFFIX_ENDURANCE,
	AFFIX_AC,

	// Regular ("white") stats
	AFFIX_STR,
	AFFIX_STA,
	AFFIX_DEX,
	AFFIX_AGI,
	AFFIX_INT,
	AFFIX_WIS,
	AFFIX_CHA,
	AFFIX_HEROIC_STR,
	AFFIX_HEROIC_STA,
	AFFIX_HEROIC_DEX,
	AFFIX_HEROIC_AGI,
	AFFIX_HEROIC_INT,
	AFFIX_HEROIC_WIS,

	// Resists, regular and heroic
	AFFIX_MAGIC_RESIST,
	AFFIX_FIRE_RESIST,
	AFFIX_COLD_RESIST,
	AFFIX_DISEASE_RESIST,
	AFFIX_POISON_RESIST,
	AFFIX_CORRUPTION_RESIST,
	AFFIX_HEROIC_MAGIC_RESIST,
	AFFIX_HEROIC_FIRE_RESIST,
	AFFIX_HEROIC_COLD_RESIST,
	AFFIX_HEROIC_DISEASE_RESIST,
	AFFIX_HEROIC_POISON_RESIST,
	AFFIX_HEROIC_CORRUPTION_RESIST,

	// Advanced item stats (item fields)
	AFFIX_SPELL_DAMAGE,
	AFFIX_HEAL_AMOUNT,
	AFFIX_ATTACK,
	AFFIX_HP_REGEN,
	AFFIX_MANA_REGEN,
	AFFIX_ENDURANCE_REGEN,
	AFFIX_COMBAT_EFFECTS,
	AFFIX_SPELL_SHIELD,
	AFFIX_SHIELDING,
	AFFIX_DAMAGE_SHIELD,
	AFFIX_DOT_SHIELDING,
	AFFIX_DS_MITIGATION,
	AFFIX_AVOIDANCE,
	AFFIX_ACCURACY,
	AFFIX_STUN_RESIST,
	AFFIX_STRIKE_THROUGH,

	// Critical hits (character bonuses, shown in the Advanced Character Stats window)
	AFFIX_SPELL_CRIT_CHANCE_PCT,
	AFFIX_SPELL_CRIT_DMG_PCT,
	AFFIX_DOT_CRIT_CHANCE_PCT,
	AFFIX_DOT_CRIT_DMG_PCT,
	AFFIX_HEAL_CRIT_CHANCE_PCT,
	AFFIX_HOT_CRIT_CHANCE_PCT,
	AFFIX_MELEE_CRIT_CHANCE_PCT,
	AFFIX_MELEE_CRIT_DMG_PCT,
	AFFIX_ARCHERY_CRIT_CHANCE_PCT,

	// Weapon Skill % Mods (Accuracy/Hit Chance)
	AFFIX_1H_SLASHING_SKILL_PCT,
	AFFIX_2H_SLASHING_SKILL_PCT,
	AFFIX_1H_BLUNT_SKILL_PCT,
	AFFIX_2H_BLUNT_SKILL_PCT,
	AFFIX_PIERCING_SKILL_PCT,
	AFFIX_HAND_TO_HAND_SKILL_PCT,
	AFFIX_ARCHERY_SKILL_PCT,

	// Defensive Skill % Mods
	AFFIX_DEFENSE_SKILL_PCT,
	AFFIX_PARRY_SKILL_PCT,
	AFFIX_RIPOSTE_SKILL_PCT,
	AFFIX_DODGE_SKILL_PCT,
	AFFIX_BLOCK_SKILL_PCT,

	// Melee Ability Skill % & Damage Mods
	AFFIX_FLYING_KICK_SKILL_PCT,
	AFFIX_FLYING_KICK_DMG_PCT,
	AFFIX_BACKSTAB_SKILL_PCT,
	AFFIX_BACKSTAB_DMG_PCT,
	AFFIX_FRENZY_SKILL_PCT,
	AFFIX_FRENZY_DMG_PCT,
	AFFIX_BASH_SKILL_PCT,
	AFFIX_BASH_DMG_PCT,

	// Spell Focus % Mods
	AFFIX_FIRE_SPELL_PCT,
	AFFIX_COLD_SPELL_PCT,
	AFFIX_MAGIC_SPELL_PCT,
	AFFIX_POISON_SPELL_PCT,
	AFFIX_DISEASE_SPELL_PCT,
	AFFIX_HEAL_AMOUNT_PCT,

	// Bard Performance % Mods
	AFFIX_STRINGED_MOD_PCT,
	AFFIX_WIND_MOD_PCT,
	AFFIX_BRASS_MOD_PCT,
	AFFIX_PERCUSSION_MOD_PCT,
	AFFIX_SINGING_MOD_PCT,

	AFFIX_MAX
};

// A character's totals of the percentage affixes on their equipped dynamic items. Flat stats
// (HP, Mana, Endurance, AC, heroics) aren't here: they're applied to the item data itself.
struct DynamicItemBonuses {
	// Weapon Skills %
	int32_t OneHandSlashingSkillPct = 0;
	int32_t TwoHandSlashingSkillPct = 0;
	int32_t OneHandBluntSkillPct = 0;
	int32_t TwoHandBluntSkillPct = 0;
	int32_t PiercingSkillPct = 0;
	int32_t HandToHandSkillPct = 0;
	int32_t ArcherySkillPct = 0;

	// Defensive Skills %
	int32_t DefenseSkillPct = 0;
	int32_t ParrySkillPct = 0;
	int32_t RiposteSkillPct = 0;
	int32_t DodgeSkillPct = 0;
	int32_t BlockSkillPct = 0;

	// Dual-Axis Melee Abilities
	int32_t FlyingKickSkillPct = 0;
	int32_t FlyingKickDmgPct = 0;
	int32_t BackstabSkillPct = 0;
	int32_t BackstabDmgPct = 0;
	int32_t FrenzySkillPct = 0;
	int32_t FrenzyDmgPct = 0;
	int32_t BashSkillPct = 0;
	int32_t BashDmgPct = 0;

	// Raw % Spell Focus
	int32_t FireDamagePct = 0;
	int32_t ColdDamagePct = 0;
	int32_t MagicDamagePct = 0;
	int32_t PoisonDamagePct = 0;
	int32_t DiseaseDamagePct = 0;
	int32_t HealAmountPct = 0;

	// Bard Performance %
	int32_t StringedModPct = 0;
	int32_t WindModPct = 0;
	int32_t BrassModPct = 0;
	int32_t PercussionModPct = 0;
	int32_t SingingModPct = 0;

	// Critical hits %
	int32_t SpellCritChancePct = 0;
	int32_t SpellCritDmgPct = 0;
	int32_t DotCritChancePct = 0;
	int32_t DotCritDmgPct = 0;
	int32_t HealCritChancePct = 0;
	int32_t HotCritChancePct = 0;
	int32_t MeleeCritChancePct = 0;
	int32_t MeleeCritDmgPct = 0;
	int32_t ArcheryCritChancePct = 0;
};
} // namespace EQ

#endif // DYNAMIC_ITEM_STRUCTS_H
