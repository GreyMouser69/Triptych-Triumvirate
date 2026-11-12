#ifndef NMS_PARAGON_H
#define NMS_PARAGON_H

class Client;
struct StatBonuses;

// AA effect IDs only this server knows: heroic stats from AAs (the Paragon line). The client has no
// spell effect for heroic stats, so these are applied server side and counted like item heroics,
// which is what every heroic stat formula reads. An AA rank's value is the total at that rank.
static constexpr int NMS_SPA_HEROIC_STR = 9001;
static constexpr int NMS_SPA_HEROIC_STA = 9002;
static constexpr int NMS_SPA_HEROIC_AGI = 9003;
static constexpr int NMS_SPA_HEROIC_DEX = 9004;
static constexpr int NMS_SPA_HEROIC_INT = 9005;
static constexpr int NMS_SPA_HEROIC_WIS = 9006;

inline bool NmsIsParagonSpa(int effect_id)
{
	return effect_id >= NMS_SPA_HEROIC_STR && effect_id <= NMS_SPA_HEROIC_WIS;
}

// Adds the heroic stats of the character's AAs to its item bonuses (before CalcHeroicBonuses)
void NmsParagonApplyBonuses(Client *c, StatBonuses *b);

#endif
