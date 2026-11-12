#include "nms_paragon.h"

#include "client.h"
#include "aa_ability.h"
#include "zone.h"

extern Zone *zone;

void NmsParagonApplyBonuses(Client *c, StatBonuses *b)
{
	if (!c || !b || !zone) {
		return;
	}

	for (const auto &aa : c->GetAAs()) {
		const auto [ability, rank] = zone->GetAlternateAdvancementAbilityAndRank(aa.first, aa.second.first);
		if (!ability || !rank || !c->HasEnabledAA(ability->id)) {
			continue;
		}

		for (const auto &e : rank->effects) {
			switch (e.effect_id) {
				case NMS_SPA_HEROIC_STR: b->HeroicSTR += e.base_value; break;
				case NMS_SPA_HEROIC_STA: b->HeroicSTA += e.base_value; break;
				case NMS_SPA_HEROIC_AGI: b->HeroicAGI += e.base_value; break;
				case NMS_SPA_HEROIC_DEX: b->HeroicDEX += e.base_value; break;
				case NMS_SPA_HEROIC_INT: b->HeroicINT += e.base_value; break;
				case NMS_SPA_HEROIC_WIS: b->HeroicWIS += e.base_value; break;
				default: break;
			}
		}
	}
}
