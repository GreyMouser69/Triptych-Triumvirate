#ifndef GEAR_SETS_H
#define GEAR_SETS_H

#include "../common/types.h"
#include <string>
#include <vector>

class Client;

// Full gear sets on top of the Bandolier window: making a bandolier set also records every worn slot,
// activating it swaps all of them. The client moves the four weapon slots itself (see SetBandolier);
// the server moves everything else and tells the client.

struct GearSetEntry {
	int16       slot_id = 0;
	uint32      item_id = 0;
	uint64      guid = 0;
	std::string custom_data;
};

std::vector<GearSetEntry> GearSetLoad(uint32 character_id, uint8 set_number);
void GearSetSave(Client *c, uint8 set_number);
void GearSetDelete(Client *c, uint8 set_number);
// include_weapons is false from the Bandolier window, which has already moved the weapons
bool GearSetApply(Client *c, uint8 set_number, bool include_weapons);

#endif
