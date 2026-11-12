#include "../client.h"
#include "../command.h"
#include "../gear_sets.h"
#include "../../common/rulesys.h"
#include "../../common/say_link.h"
#include "../../common/timer.h"

#include <map>

// #gear: the full gear sets saved with the Bandolier window. Sets are numbered 1-20 in window order;
// make, rename and delete them in the Bandolier window.
namespace {
	const char *SlotName(int16 slot_id)
	{
		static const char *names[] = {
			"Charm", "Ear", "Head", "Face", "Ear", "Neck", "Shoulders", "Arms", "Back", "Wrist", "Wrist", "Range",
			"Hands", "Primary", "Secondary", "Finger", "Finger", "Chest", "Legs", "Feet", "Waist", "Power Source", "Ammo"
		};
		return (slot_id >= 0 && slot_id < static_cast<int16>(sizeof(names) / sizeof(names[0]))) ? names[slot_id] : "?";
	}

	bool OnCharacter(Client *c, const GearSetEntry &e)
	{
		return c->GetInv().HasItem(e.item_id, 1, invWhereWorn | invWherePersonal) != INVALID_INDEX;
	}

	void GearList(Client *c)
	{
		int shown = 0;
		for (uint8 n = 0; n < EQ::profile::BANDOLIERS_SIZE; ++n) {
			const std::string name = c->GetBandolierName(n);
			if (name.empty()) {
				continue;
			}
			const auto set = GearSetLoad(c->CharacterID(), n);
			int missing = 0;
			for (const auto &e : set) {
				if (!OnCharacter(c, e)) {
					++missing;
				}
			}
			c->Message(
				Chat::White,
				fmt::format(
					"{}. {} - {} piece{}{} {} {}",
					n + 1, name, set.size(), set.size() == 1 ? "" : "s",
					set.empty() ? " (weapons only: remake it to save the full set)" :
						(missing ? fmt::format(", {} not on you", missing) : ""),
					Saylink::Create(fmt::format("#gear show {}", n + 1), true, "show"),
					Saylink::Create(fmt::format("#gear load {}", n + 1), true, "load")
				).c_str()
			);
			++shown;
		}
		if (!shown) {
			c->Message(Chat::White, "You have no gear sets. Equip a set, open the Bandolier window and make a set: it saves every worn slot.");
		}
	}

	void GearShow(Client *c, uint8 n)
	{
		const auto set = GearSetLoad(c->CharacterID(), n);
		if (set.empty()) {
			c->Message(Chat::White, "That gear set has no saved gear. Equip it and remake it in the Bandolier window.");
			return;
		}
		c->Message(Chat::White, fmt::format("Gear set {}. {}:", n + 1, c->GetBandolierName(n)).c_str());
		for (const auto &e : set) {
			const auto *item = database.GetItem(e.item_id);
			c->Message(
				Chat::White,
				fmt::format(
					"  {}: {}{}", SlotName(e.slot_id), item ? item->Name : std::to_string(e.item_id),
					OnCharacter(c, e) ? "" : " (not on you)"
				).c_str()
			);
		}
	}
}

void command_gear(Client *c, const Seperator *sep)
{
	const std::string sub = sep->arg[1];
	if (sub.empty() || sub == "list") {
		GearList(c);
		return;
	}

	const int n = sep->IsNumber(2) ? Strings::ToInt(sep->arg[2]) - 1 : -1;
	if ((sub != "show" && sub != "load") || n < 0 || n >= EQ::profile::BANDOLIERS_SIZE || c->GetBandolierName(n).empty()) {
		c->Message(Chat::White, "Usage: #gear [list] | #gear show [set number] | #gear load [set number]");
		return;
	}

	if (sub == "show") {
		GearShow(c, n);
		return;
	}

	// Same swap delay as the Bandolier window
	static std::map<uint32, uint32> last_load;
	const uint32 now = Timer::GetCurrentTime();
	const uint32 delay = static_cast<uint32>(std::max(RuleI(Character, BandolierSwapDelay), 0));
	auto it = last_load.find(c->CharacterID());
	if (delay && it != last_load.end() && now - it->second < delay) {
		c->Message(Chat::White, "You can't swap gear sets again yet.");
		return;
	}
	last_load[c->CharacterID()] = now;

	if (GearSetLoad(c->CharacterID(), n).empty()) {
		c->Message(Chat::White, "That gear set has no saved gear. Equip it and remake it in the Bandolier window.");
		return;
	}
	GearSetApply(c, n, true);
}
