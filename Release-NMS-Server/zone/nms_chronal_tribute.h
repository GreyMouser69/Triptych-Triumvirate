#ifndef NMS_CHRONAL_TRIBUTE_H
#define NMS_CHRONAL_TRIBUTE_H

#include "../common/types.h"

class Client;

// Chronal Tributes (Custom:ChronalTributeFirstID - LastID) are tributes that only work inside a Greater
// Rift. Tribute tiers stack by band of rift tiers (Custom:ChronalTributeBandSize): tier 3 turns on bands
// 1-3, so it works in rift tiers 1-300, giving the benefit of the rift's own band (tier 1's in a tier 50
// rift, tier 3's in a tier 250 rift). Above its top band, and outside rifts, it does nothing. Instead of the usual
// favor every 10 minutes, each one costs Custom:ChronalTributeFavorPerTier x the rift tier once per run,
// taken on entering the rift (or on turning the tribute on inside it), so the price follows the income.
bool NmsIsChronalTribute(uint32 tribute_id);

// Works out which of the character's Chronal Tributes apply in this zone and charges for any not yet
// paid for in this rift. Called from DoTributeUpdate (zone-in, tribute changes, the 10 minute renewal).
void NmsChronalTributeRefresh(Client *c);

// Whether the Chronal Tribute in this tribute slot applies right now (false outside a rift, or above its top band)
bool NmsChronalTributeActive(const Client *c, int slot);

// The tribute tier whose benefit an active Chronal Tribute gives in this rift (the rift's band)
uint32 NmsChronalTributeTier(const Client *c, uint32 selected_tier);

#endif
