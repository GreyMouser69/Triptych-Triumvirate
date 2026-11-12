#include "dynamic_items.h"

#include <algorithm>
#include <cmath>
#include <random>

using namespace EQ;

// -------------------------------------------------------------------
// Every random number an item uses is derived from (seed, affix, purpose) by hashing rather
// than by drawing from a shared generator in order. That keeps lines independent: editing one
// line's range in dynamic_affix_pool rescales only that line, and adding or reweighting a pool
// entry can't shift the values of the others.
// -------------------------------------------------------------------
namespace {
	enum RollPurpose : uint64_t {
		PURPOSE_VALUE = 1,
		PURPOSE_PICK  = 2,
		PURPOSE_COUNT = 3,
		PURPOSE_REFORGE_PICK = 4,
	};

	uint64_t SplitMix64(uint64_t x)
	{
		x += 0x9E3779B97F4A7C15ULL;
		x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
		x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
		return x ^ (x >> 31);
	}

	// FNV-1a; affixes are keyed by their table code so the enum order never matters
	uint64_t HashCode(const char *s)
	{
		uint64_t h = 0xCBF29CE484222325ULL;
		for (; *s; ++s) {
			h ^= static_cast<unsigned char>(*s);
			h *= 0x100000001B3ULL;
		}

		return h;
	}

	// Uniform in [0, 1)
	double Unit(uint64_t seed, uint64_t key, uint64_t purpose)
	{
		const uint64_t h = SplitMix64(seed ^ SplitMix64(key ^ SplitMix64(purpose)));
		return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
	}

	bool FitsSlot(const DynamicAffixDef &line, uint32 item_slots)
	{
		return line.slot_mask == 0 || (line.slot_mask & item_slots) != 0;
	}

	int32 RollValue(uint64_t seed, const DynamicAffixDef &line, float multiplier, double &position)
	{
		position = Unit(seed, HashCode(DynamicItemsManager::AffixCode(line.affix)), PURPOSE_VALUE);
		const double span     = static_cast<double>(line.max_value) - line.min_value + 1.0;
		const double base     = std::min<double>(line.min_value + std::floor(position * span), line.max_value);

		auto value = static_cast<int32>(std::lround(base * multiplier));

		// Low-weight stats are small, never zero
		if (line.min_value > 0 && value < 1) {
			value = 1;
		}

		return value;
	}
}

// -------------------------------------------------------------------
// Seed generator (called once, when the item is bought)
// -------------------------------------------------------------------
uint64_t GenerateNewItemSeed()
{
	std::random_device rd;
	const uint64_t seed = (static_cast<uint64_t>(rd()) << 32) | static_cast<uint64_t>(rd());
	return seed == 0 ? 0x123456789ABCDEF0ULL : seed; // 0 means "not rolled"
}

// -------------------------------------------------------------------
// Seed -> stat lines. Guaranteed lines always roll; random affixes are picked by weight
// without repeats (weighted reservoir sampling), then everything is listed in affix order.
// -------------------------------------------------------------------
std::vector<RolledAffix> RollDynamicItem(
	uint64_t seed,
	const DynamicItemDef &def,
	const std::vector<DynamicAffixDef> &pool,
	uint32 item_slots
)
{
	std::vector<RolledAffix> rolled;
	if (seed == 0) {
		return rolled;
	}

	std::vector<std::pair<double, const DynamicAffixDef *>> candidates;

	for (const auto &line : pool) {
		if (!FitsSlot(line, item_slots)) {
			continue;
		}

		if (line.guaranteed) {
			double position = 0.0;
			const auto value = RollValue(seed, line, def.stat_multiplier, position);

			// Lines whose range starts at 0 can roll 0: the item just doesn't have that stat
			if (value != 0) {
				rolled.push_back({ line.affix, value, true, position });
			}
		}
		else if (static_cast<int32_t>(line.weight) > 0) {
			const double u   = 1.0 - Unit(seed, HashCode(DynamicItemsManager::AffixCode(line.affix)), PURPOSE_PICK); // (0, 1]
			const double key = std::log(u) / static_cast<double>(line.weight);
			candidates.emplace_back(key, &line);
		}
	}

	const uint32 span  = static_cast<uint32>(def.max_affixes - def.min_affixes) + 1;
	uint32       count = def.min_affixes + static_cast<uint32>(Unit(seed, 0, PURPOSE_COUNT) * span);
	count = std::min<uint32>(count, static_cast<uint32>(candidates.size()));

	std::sort(
		candidates.begin(),
		candidates.end(),
		[](const auto &a, const auto &b) { return a.first > b.first; }
	);

	for (uint32 i = 0; i < count; ++i) {
		const auto &line     = *candidates[i].second;
		double      position = 0.0;
		const auto  value    = RollValue(seed, line, def.stat_multiplier, position);
		rolled.push_back({ line.affix, value, false, position });
	}

	std::sort(
		rolled.begin(),
		rolled.end(),
		[](const RolledAffix &a, const RolledAffix &b) { return a.affix < b.affix; }
	);

	return rolled;
}

// -------------------------------------------------------------------
// #reforge
// -------------------------------------------------------------------
void ApplyDynamicEnchant(
	std::vector<RolledAffix> &rolled,
	const DynamicEnchant &enchant,
	const DynamicItemDef &def,
	const std::vector<DynamicAffixDef> &pool,
	uint32 item_slots
)
{
	// enchant.from must name the line being reforged; an item that has never been
	// reforged (from == AFFIX_NONE) has nothing to replace, so this is a no-op.
	if (enchant.from == AFFIX_NONE || enchant.to == AFFIX_NONE || enchant.seed == 0) {
		return;
	}

	auto target = std::find_if(
		rolled.begin(),
		rolled.end(),
		[&](const RolledAffix &l) { return !l.guaranteed && l.affix == enchant.from; }
	);
	if (target == rolled.end()) {
		return;
	}

	// The replacement can't duplicate another line (the pool may have changed since).
	// Self-replacement (enchant.from == enchant.to) is intentionally allowed: the
	// target is excluded from this check by pointer, so the same affix is re-rolled
	// with a fresh value instead of being rejected as a duplicate.
	for (const auto &l : rolled) {
		if (&l != &*target && l.affix == enchant.to) {
			return;
		}
	}

	const auto line = std::find_if(
		pool.begin(),
		pool.end(),
		[&](const DynamicAffixDef &d) { return d.affix == enchant.to && !d.guaranteed && FitsSlot(d, item_slots); }
	);
	if (line == pool.end()) {
		return;
	}

	double position = 0.0;
	target->affix    = enchant.to;
	target->value    = RollValue(enchant.seed, *line, def.stat_multiplier, position);
	target->position = position;
	target->reforged = true;

	std::sort(
		rolled.begin(),
		rolled.end(),
		[](const RolledAffix &a, const RolledAffix &b) { return a.affix < b.affix; }
	);
}

std::vector<RolledAffix> RollReforgeOptions(
	uint64_t seed,
	const std::vector<EQ::eDynamicAffixID> &taken,
	const DynamicItemDef &def,
	const std::vector<DynamicAffixDef> &pool,
	uint32 item_slots,
	uint32 count
)
{
	std::vector<std::pair<double, const DynamicAffixDef *>> candidates;
	// Pool entries that share an affix must not produce duplicate options. Track the
	// affixes already accepted so only the first qualifying entry per affix is kept.
	std::vector<EQ::eDynamicAffixID> seen_affixes;
	for (const auto &line : pool) {
		if (line.guaranteed ||
			line.weight == 0 ||
			static_cast<int32_t>(line.weight) < 0 ||
			!FitsSlot(line, item_slots) ||
			std::find(taken.begin(), taken.end(), line.affix) != taken.end()) {
			continue;
		}

		if (std::find(seen_affixes.begin(), seen_affixes.end(), line.affix) != seen_affixes.end()) {
			continue;
		}
		seen_affixes.push_back(line.affix);

		const double u = 1.0 - Unit(seed, HashCode(DynamicItemsManager::AffixCode(line.affix)), PURPOSE_REFORGE_PICK); // (0, 1]
		candidates.emplace_back(std::log(u) / static_cast<double>(line.weight), &line);
	}

	std::sort(
		candidates.begin(),
		candidates.end(),
		[](const auto &a, const auto &b) { return a.first > b.first; }
	);

	std::vector<RolledAffix> options;
	for (uint32 i = 0; i < count && i < candidates.size(); ++i) {
		const auto &line     = *candidates[i].second;
		double      position = 0.0;
		const auto  value    = RollValue(seed, line, def.stat_multiplier, position);
		options.push_back({ line.affix, value, false, position, true });
	}

	return options;
}
