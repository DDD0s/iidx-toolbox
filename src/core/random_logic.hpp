#pragma once

// Random Fix lane-map logic. No game dependencies (test/random_logic_test.cpp).

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace tb::random_logic
{
	enum class arrange_t : int32_t
	{
		off = 0,
		random = 1,
		r_random = 2,
		s_random = 3,
	};

	using perm_t = std::array<int32_t, 7>;

	// One side. perm[k] = screen lane of chart lane k (0-based, keys only).
	struct side_config_t
	{
		perm_t perm{ 0, 1, 2, 3, 4, 5, 6 };
		bool ran = false;   // draw a new arrangement at every play start
		bool group = false; // same, but keep each lane's key colour (wins over ran)
	};

	inline bool is_valid_perm(const perm_t& perm)
	{
		bool seen[7] = {};
		for (const int32_t v : perm)
		{
			if (v < 0 || v > 6 || seen[v])
				return false;
			seen[v] = true;
		}
		return true;
	}

	inline perm_t inverse(const perm_t& perm)
	{
		perm_t inv{};
		for (int32_t k = 0; k < 7; ++k)
			inv[static_cast<size_t>(perm[static_cast<size_t>(k)])] = k;
		return inv;
	}

	// Keys 1, 3, 5, 7 (index 0, 2, 4, 6) are white, 2, 4, 6 black.
	inline bool is_white_key(int32_t key)
	{
		return (key % 2) == 0;
	}

	// Colour of each screen lane, left to right: "WWWWBBB" and the like.
	inline std::string color_pattern(const perm_t& perm)
	{
		const perm_t screen = inverse(perm);
		std::string out(7, 'W');
		for (size_t j = 0; j < 7; ++j)
			out[j] = is_white_key(screen[j]) ? 'W' : 'B';
		return out;
	}

	// "2134675": digit k is the 1-based value at index k. Rejects anything that
	// is not a permutation of 1-7.
	inline bool parse_digits(std::string_view text, perm_t& out)
	{
		if (text.size() != 7)
			return false;
		perm_t parsed{};
		for (size_t i = 0; i < 7; ++i)
		{
			if (text[i] < '1' || text[i] > '7')
				return false;
			parsed[i] = text[i] - '1';
		}
		if (!is_valid_perm(parsed))
			return false;
		out = parsed;
		return true;
	}

	inline std::string to_digits(const perm_t& values)
	{
		std::string out(7, '1');
		for (size_t i = 0; i < 7; ++i)
			out[i] = static_cast<char>('1' + values[i]);
		return out;
	}

	// Screen order, the way players write a random: digit j is the key whose
	// notes appear in screen lane j, left to right, on either side.
	inline bool parse_screen_order(std::string_view text, perm_t& perm_out)
	{
		perm_t screen{};
		if (!parse_digits(text, screen))
			return false;
		perm_out = inverse(screen);
		return true;
	}

	// the same keys read right to left on screen; flipping twice restores the order
	inline perm_t flip(const perm_t& perm)
	{
		const perm_t screen = inverse(perm);
		perm_t flipped{};
		for (size_t slot = 0; slot < 7; ++slot)
			flipped[slot] = screen[6 - slot];
		return inverse(flipped);
	}

	inline std::string screen_order(const perm_t& perm)
	{
		return to_digits(inverse(perm));
	}

	// Uniform random arrangement; rng() returns uniformly distributed uint32.
	template <typename Rng>
	perm_t random_perm(Rng& rng)
	{
		perm_t p{ 0, 1, 2, 3, 4, 5, 6 };
		for (int32_t i = 6; i > 0; --i)
		{
			const auto j = static_cast<int32_t>(rng() % static_cast<uint32_t>(i + 1));
			std::swap(p[static_cast<size_t>(i)], p[static_cast<size_t>(j)]);
		}
		return p;
	}

	// Random arrangement that keeps current's colour pattern: every white lane
	// gets a random white key, every black lane a random black key.
	template <typename Rng>
	perm_t group_random_perm(const perm_t& current, Rng& rng)
	{
		if (!is_valid_perm(current))
			return current;
		std::array<int32_t, 4> whites{ 0, 2, 4, 6 };
		std::array<int32_t, 3> blacks{ 1, 3, 5 };
		const auto shuffle = [&rng](auto& keys) {
			for (size_t i = keys.size() - 1; i > 0; --i)
			{
				const auto j = static_cast<size_t>(rng() % static_cast<uint32_t>(i + 1));
				std::swap(keys[i], keys[j]);
			}
		};
		shuffle(whites);
		shuffle(blacks);

		const perm_t screen = inverse(current);
		perm_t drawn{};
		size_t next_white = 0, next_black = 0;
		for (size_t j = 0; j < 7; ++j)
			drawn[j] = is_white_key(screen[j]) ? whites[next_white++] : blacks[next_black++];
		return inverse(drawn);
	}

	// All 7 keys shuffled and scratch fixed. Other pools (5KEYS, autoscratch)
	// stay native so the map remains a bijection.
	inline bool pool_is_standard(const uint8_t flags[8])
	{
		for (int i = 0; i < 7; ++i)
			if (flags[i] == 0)
				return false;
		return flags[7] == 0;
	}

	enum class apply_result
	{
		skipped_disabled,
		skipped_arrange, // anything but RANDOM stays native, R-RANDOM included
		skipped_autoscratch,
		skipped_pool,
		skipped_invalid,
		applied_random,
	};

	inline apply_result check_applicable(const uint8_t flags[8], arrange_t arrange, bool autoscratch, bool enabled)
	{
		if (!enabled)
			return apply_result::skipped_disabled;
		if (arrange != arrange_t::random)
			return apply_result::skipped_arrange;
		if (autoscratch)
			return apply_result::skipped_autoscratch;
		if (!pool_is_standard(flags))
			return apply_result::skipped_pool;
		return apply_result::applied_random;
	}

	// Overwrite one side's map (screen = map[chart lane], lane 7 = scratch).
	// Runs after the game's own apply, so mirror is composed again here.
	inline apply_result apply_to_map(std::array<int32_t, 8>& map, const uint8_t flags[8],
		arrange_t arrange, bool mirror, bool autoscratch, const side_config_t& cfg, bool enabled)
	{
		const auto check = check_applicable(flags, arrange, autoscratch, enabled);
		if (check != apply_result::applied_random)
			return check;
		if (!is_valid_perm(cfg.perm))
			return apply_result::skipped_invalid;

		for (size_t k = 0; k < 7; ++k)
			map[k] = mirror ? 6 - cfg.perm[k] : cfg.perm[k];
		map[7] = 7;
		return apply_result::applied_random;
	}
}