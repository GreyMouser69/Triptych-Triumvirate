#include "nms_chronal_tribute.h"

#include "../common/data_bucket.h"
#include "../common/rulesys.h"
#include "../common/strings.h"
#include "client.h"
#include "zone.h"

#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>

extern Zone *zone;
extern std::map<uint32, TributeData> tribute_list;

namespace {
	// Character id -> tribute slots whose Chronal Tribute applies in this zone, and the band (tribute tier)
	// they give. A zone process is one instance, so this is rebuilt on every zone-in.
	struct ActiveChronal {
		uint32 slots = 0;
		uint32 tier  = 0;
	};
	std::unordered_map<uint32, ActiveChronal> active;

	constexpr const char *PAID_BUCKET    = "chronal_tribute_paid"; // "instance id|tribute id,tribute id"
	constexpr const char *ACTIVE_BUCKET  = "chronal_tributes";     // "instance id|tribute id:tier,..." for the rift Lua (Fortune)
	constexpr const char *BUCKET_EXPIRES = "7200";

	// The rift tier of this zone's instance, 0 if it isn't a Greater Rift
	int GetRiftTier()
	{
		if (!zone || zone->GetInstanceID() == 0) {
			return 0;
		}

		const auto tier = DataBucket::GetData(fmt::format("gr_tier_{}", zone->GetInstanceID()));
		return Strings::IsNumber(tier) ? Strings::ToInt(tier) : 0;
	}

	std::string BandText(int tier_index)
	{
		const int band = std::max(RuleI(Custom, ChronalTributeBandSize), 1);
		return fmt::format("Tiers 1-{}", (tier_index + 1) * band);
	}
}

bool NmsIsChronalTribute(uint32 tribute_id)
{
	return tribute_id != TRIBUTE_NONE &&
		tribute_id >= static_cast<uint32>(RuleI(Custom, ChronalTributeFirstID)) &&
		tribute_id <= static_cast<uint32>(RuleI(Custom, ChronalTributeLastID));
}

bool NmsChronalTributeActive(const Client *c, int slot)
{
	const auto it = active.find(c->CharacterID());
	return it != active.end() && (it->second.slots & (1u << slot)) != 0;
}

uint32 NmsChronalTributeTier(const Client *c, uint32 selected_tier)
{
	const auto it = active.find(c->CharacterID());
	if (it == active.end()) {
		return 0;
	}
	// active.tier is a 0-based rift band index; selected_tier is a 1-based EQ tribute tier.
	// The benefit is the rift's band expressed as a 1-based tribute tier, clamped to the
	// tribute's selected tier.
	const uint32 rift_band = it->second.tier + 1;
	return std::min(rift_band, selected_tier);
}

void NmsChronalTributeRefresh(Client *c)
{
	if (!c) {
		return;
	}

	uint32 mask = 0;
	auto   &pp  = c->GetPP();

	const int rift_tier = GetRiftTier();
	if (!pp.tribute_active || rift_tier <= 0) {
		active[c->CharacterID()] = {};
		return;
	}

	const auto instance_id = zone->GetInstanceID();
	const int  band        = std::max(RuleI(Custom, ChronalTributeBandSize), 1);
	const int  band_index  = (rift_tier - 1) / band;

	// Tributes already paid for in this rift
	std::set<uint32> paid;
	const auto paid_parts = Strings::Split(c->GetBucket(PAID_BUCKET), '|');
	if (paid_parts.size() == 2 && paid_parts[0] == std::to_string(instance_id)) {
		for (const auto &id : Strings::Split(paid_parts[1], ',')) {
			if (Strings::IsNumber(id)) {
				paid.insert(Strings::ToUnsignedInt(id));
			}
		}
	}

	std::vector<int> to_charge;
	for (int r = 0; r < EQ::invtype::TRIBUTE_SIZE; r++) {
		const uint32 tid = pp.tributes[r].tribute;
		if (!NmsIsChronalTribute(tid) || !tribute_list.contains(tid)) {
			continue;
		}

		// Tiers stack: the selected tier turns on its band and every band below it
		const int tier_index = static_cast<int>(pp.tributes[r].tier) - 1;
		if (tier_index < band_index) {
			c->Message(
				Chat::Yellow,
				"%s",
				fmt::format(
					"{} ({}) lies dormant in this Tier {} Fracture.",
					tribute_list[tid].name,
					BandText(tier_index),
					rift_tier
				).c_str()
			);
			continue;
		}

		if (paid.contains(tid)) {
			mask |= 1u << r;
		}
		else {
			to_charge.push_back(r);
		}
	}

	if (!to_charge.empty()) {
		const uint32 each  = static_cast<uint32>(std::max(RuleI(Custom, ChronalTributeFavorPerTier), 0)) * static_cast<uint32>(rift_tier);
		const uint32 total = each * static_cast<uint32>(to_charge.size());
		if (pp.tribute_points < total) {
			c->Message(
				Chat::Red,
				"%s",
				fmt::format(
					"Your Chronal Tributes need {} favor for this Tier {} Fracture; you have {}. They lie dormant.",
					Strings::Commify(total),
					rift_tier,
					Strings::Commify(pp.tribute_points)
				).c_str()
			);
		}
		else {
			c->AddTributePoints(-static_cast<int32>(total));
			for (int r : to_charge) {
				mask |= 1u << r;
				paid.insert(pp.tributes[r].tribute);
			}

			c->Message(
				Chat::Yellow,
				"%s",
				fmt::format(
					"Your Chronal Tributes draw {} favor for this Tier {} Fracture.",
					Strings::Commify(total),
					rift_tier
				).c_str()
			);

			std::vector<std::string> ids;
			for (auto id : paid) {
				ids.push_back(std::to_string(id));
			}
			c->SetBucket(PAID_BUCKET, fmt::format("{}|{}", instance_id, Strings::Join(ids, ",")), BUCKET_EXPIRES);
		}
	}

	active[c->CharacterID()] = { mask, static_cast<uint32>(band_index) };

	// What's active and at which tier, for the rift scripts (Chronal Fortune adds shards to the clear reward)
	std::vector<std::string> list;
	for (int r = 0; r < EQ::invtype::TRIBUTE_SIZE; r++) {
		if (mask & (1u << r)) {
			list.push_back(fmt::format("{}:{}", pp.tributes[r].tribute, band_index + 1));
		}
	}
	c->SetBucket(ACTIVE_BUCKET, fmt::format("{}|{}", instance_id, Strings::Join(list, ",")), BUCKET_EXPIRES);
}
