// Unit test for the Random Fix lane-map logic.
//
// Standalone: not part of the client premake projects (client only globs
// src/client/**). Build and run manually, e.g. from a VS developer prompt:
//   cl /std:c++20 /EHsc /W4 /I src\client\component\iidx ^
//      tests\random_logic_test.cpp /Fe:random_logic_test.exe
//   random_logic_test.exe
// Exit code 0 = all checks passed.

#include "random_logic.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>

namespace cr = tb::random_logic;

static int g_failures = 0;

#define CHECK(...)                                                          \
	do {                                                                    \
		if (!(__VA_ARGS__)) {                                               \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__); \
			++g_failures;                                                   \
		}                                                                   \
	} while (0)

static bool map_is_bijection(const std::array<int32_t, 8>& m)
{
	bool seen[8] = {};
	for (const int32_t v : m)
	{
		if (v < 0 || v > 7 || seen[v])
			return false;
		seen[v] = true;
	}
	return true;
}

int main()
{
	using perm_t = std::array<int32_t, 7>;

	// ---- digits and screen order ----
	perm_t p{};
	CHECK(cr::parse_digits("1234567", p) && p == perm_t{0, 1, 2, 3, 4, 5, 6});
	CHECK(cr::parse_digits("3142756", p) && p == perm_t{2, 0, 3, 1, 6, 4, 5});
	CHECK(!cr::parse_digits("", p));
	CHECK(!cr::parse_digits("123456", p));
	CHECK(!cr::parse_digits("12345678", p));
	CHECK(!cr::parse_digits("1123456", p));
	CHECK(!cr::parse_digits("1234560", p));
	CHECK(!cr::parse_digits("123456a", p));

	// screen order "2134675": lane 1 shows key 2, lane 2 key 1, ... lane 7 key 5
	CHECK(cr::parse_screen_order("2134675", p));
	CHECK(p[1] == 0 && p[0] == 1 && p[4] == 6 && p[5] == 4 && p[6] == 5);
	CHECK(cr::screen_order(p) == "2134675");
	CHECK(cr::parse_screen_order("7654321", p) && p == perm_t{6, 5, 4, 3, 2, 1, 0});
	CHECK(cr::parse_screen_order("1234567", p) && cr::screen_order(p) == "1234567");

	// flip: screen order read right to left, colours mirrored, twice = unchanged
	{
		perm_t f{};
		CHECK(cr::parse_screen_order("3572461", f));
		const perm_t flipped = cr::flip(f);
		CHECK(cr::is_valid_perm(flipped));
		CHECK(cr::screen_order(flipped) == "1642753");
		const std::string pattern = cr::color_pattern(f);
		CHECK(cr::color_pattern(flipped) == std::string(pattern.rbegin(), pattern.rend()));
		CHECK(cr::flip(flipped) == f);
		CHECK(cr::screen_order(cr::flip(perm_t{0, 1, 2, 3, 4, 5, 6})) == "7654321");
	}

	CHECK(cr::is_valid_perm(perm_t{0, 1, 2, 3, 4, 5, 6}));
	CHECK(!cr::is_valid_perm(perm_t{0, 0, 2, 3, 4, 5, 6}));
	CHECK(!cr::is_valid_perm(perm_t{0, 1, 2, 3, 4, 5, 7}));

	// random draws are permutations and not constant
	{
		uint32_t state = 12345;
		auto rng = [&state]() { state = state * 1664525u + 1013904223u; return state >> 8; };
		perm_t first = cr::random_perm(rng);
		bool all_valid = cr::is_valid_perm(first), differs = false;
		for (int i = 0; i < 50; ++i)
		{
			const perm_t q = cr::random_perm(rng);
			all_valid = all_valid && cr::is_valid_perm(q);
			differs = differs || q != first;
		}
		CHECK(all_valid && differs);
	}

	// ---- group (colour-keeping) draws ----
	CHECK(cr::color_pattern(perm_t{0, 1, 2, 3, 4, 5, 6}) == "WBWBWBW");
	CHECK(cr::parse_screen_order("1357246", p) && cr::color_pattern(p) == "WWWWBBB");
	CHECK(cr::parse_screen_order("2461357", p) && cr::color_pattern(p) == "BBBWWWW");
	{
		std::mt19937 rng(20261015u);
		const char* patterns[] = { "1357246", "2461357", "1234567", "7254163", "1325476" };
		for (const char* text : patterns)
		{
			perm_t start{};
			CHECK(cr::parse_screen_order(text, start));
			const std::string want = cr::color_pattern(start);

			// counts[slot][key]: how often each key lands in each lane
			int counts[7][7] = {};
			bool all_ok = true, any_change = false;
			constexpr int draws = 42000;
			perm_t cur = start;
			for (int i = 0; i < draws; ++i)
			{
				cur = cr::group_random_perm(cur, rng); // chained, like play after play
				all_ok = all_ok && cr::is_valid_perm(cur) && cr::color_pattern(cur) == want;
				any_change = any_change || cur != start;
				const perm_t screen = cr::inverse(cur);
				for (int j = 0; j < 7; ++j)
					++counts[j][screen[static_cast<size_t>(j)]];
			}
			CHECK(all_ok);
			CHECK(any_change);

			// uniform within the colour: white lanes see each white key ~1/4 of
			// the time, black lanes each black key ~1/3, never the other colour
			bool uniform = true;
			for (int j = 0; j < 7; ++j)
			{
				const bool white_lane = want[static_cast<size_t>(j)] == 'W';
				for (int key = 0; key < 7; ++key)
				{
					const int n = counts[j][key];
					if (cr::is_white_key(key) != white_lane)
						uniform = uniform && n == 0;
					else
					{
						const double expect = draws / (white_lane ? 4.0 : 3.0);
						uniform = uniform && n > expect * 0.95 && n < expect * 1.05;
					}
				}
			}
			CHECK(uniform);
		}

		// an invalid stored perm is returned unchanged (apply_to_map rejects it)
		const perm_t bad{0, 0, 0, 0, 0, 0, 0};
		CHECK(cr::group_random_perm(bad, rng) == bad);
	}

	// ---- apply_to_map ----
	const uint8_t std_flags[8] = {1, 1, 1, 1, 1, 1, 1, 0};
	cr::side_config_t cfg{};
	cfg.perm = perm_t{6, 5, 4, 3, 2, 1, 0};

	std::array<int32_t, 8> m{0, 1, 2, 3, 4, 5, 6, 7};
	CHECK(cr::apply_to_map(m, std_flags, cr::arrange_t::random, false, false, cfg, true)
		== cr::apply_result::applied_random);
	CHECK(m == std::array<int32_t, 8>{6, 5, 4, 3, 2, 1, 0, 7});
	CHECK(map_is_bijection(m));

	// mirror composes on values after the custom perm (native does the same)
	m = {0, 1, 2, 3, 4, 5, 6, 7};
	cr::side_config_t id_cfg{};
	CHECK(cr::apply_to_map(m, std_flags, cr::arrange_t::random, true, false, id_cfg, true)
		== cr::apply_result::applied_random);
	CHECK(m == std::array<int32_t, 8>{6, 5, 4, 3, 2, 1, 0, 7});

	// R-RANDOM stays native
	m = {0, 1, 2, 3, 4, 5, 6, 7};
	CHECK(cr::apply_to_map(m, std_flags, cr::arrange_t::r_random, false, false, cfg, true)
		== cr::apply_result::skipped_arrange);

	// untouched cases: disabled, OFF, S-RANDOM, autoscratch, bad pool
	const auto untouched = std::array<int32_t, 8>{3, 1, 4, 0, 6, 2, 5, 7};
	m = untouched;
	CHECK(cr::apply_to_map(m, std_flags, cr::arrange_t::random, false, false, cfg, false)
		== cr::apply_result::skipped_disabled);
	CHECK(m == untouched);
	CHECK(cr::apply_to_map(m, std_flags, cr::arrange_t::off, false, false, cfg, true)
		== cr::apply_result::skipped_arrange);
	CHECK(m == untouched);
	CHECK(cr::apply_to_map(m, std_flags, cr::arrange_t::s_random, false, false, cfg, true)
		== cr::apply_result::skipped_arrange);
	CHECK(m == untouched);
	CHECK(cr::apply_to_map(m, std_flags, cr::arrange_t::random, false, true, cfg, true)
		== cr::apply_result::skipped_autoscratch);
	CHECK(m == untouched);

	const uint8_t fivekeys_flags[8] = {1, 1, 1, 1, 1, 0, 0, 0};
	CHECK(cr::apply_to_map(m, fivekeys_flags, cr::arrange_t::random, false, false, cfg, true)
		== cr::apply_result::skipped_pool);
	CHECK(m == untouched);
	const uint8_t scr_flags[8] = {1, 1, 1, 1, 1, 1, 1, 1};
	CHECK(cr::apply_to_map(m, scr_flags, cr::arrange_t::random, false, false, cfg, true)
		== cr::apply_result::skipped_pool);
	CHECK(m == untouched);

	// invalid stored perm fails closed
	cr::side_config_t bad_cfg{};
	bad_cfg.perm = perm_t{0, 0, 0, 0, 0, 0, 0};
	CHECK(cr::apply_to_map(m, std_flags, cr::arrange_t::random, false, false, bad_cfg, true)
		== cr::apply_result::skipped_invalid);
	CHECK(m == untouched);

	if (g_failures == 0)
		std::printf("random_logic_test: all checks passed\n");
	else
		std::printf("random_logic_test: %d check(s) FAILED\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}