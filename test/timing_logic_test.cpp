// MS CLOCK against the game's own step (sub_180743DE0): +1 tick per step, and at every
// second step +1 more when the audio position leads the chart by more than 17 ms.
// Checks that in normal play the chart is the game's, frame for frame, that after
// stalls it ends at the game's phase, and that it gets there in fewer frames.

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "timing_logic.hpp"

namespace
{
	using tb::timing_logic::chart_ms;
	using tb::timing_logic::extra_ticks;

	constexpr double k_period = 1000.0 / 120.0;
	constexpr float k_fps = 120.f;

	struct scenario_t
	{
		const char* name;
		double lead0;       // audio lead at the game's check at song start, ms
		double drift_ppm;   // audio clock against the display
		double jitter_ms;   // audio position noise
		double small_prob;  // absorbed stalls: the step runs late, no frame is lost
		double small_max_ms;
		int big_at;         // frame of one long stall that loses frames (0 = none)
		double big_ms;
	};

	struct run_t
	{
		std::vector<int32_t> ticks;
		std::vector<double> leads; // audio at the step minus chart time of the processed tick
		int removed = 0;
	};

	run_t run(const scenario_t& sc, bool ms_clock, unsigned seed, int parity)
	{
		std::mt19937 rng(seed);
		std::uniform_real_distribution<double> u(0.0, 1.0);
		const int frames = 120 * 60;
		run_t r;
		int32_t tick = 1, target = 2;
		bool check = false;
		double shift = 0.0;   // time lost to dropped frames
		double refill = 0.0;  // lateness of an absorbed stall still being made up
		for (int f = 1; f < frames; ++f)
		{
			double late = 0.3 + 0.3 * u(rng);
			const double noise = (u(rng) * 2.0 - 1.0) * sc.jitter_ms;
			const double small = u(rng) < sc.small_prob ? 1.0 + (sc.small_max_ms - 1.0) * u(rng) : 0.0;
			if (f == sc.big_at)
			{
				late += sc.big_ms;
				shift += std::floor(sc.big_ms / k_period) * k_period;
			}
			refill = std::max(0.0, refill - 1.0); // the limiter makes up about 1 ms per frame
			refill += small;
			const double step_time = f * k_period + shift + late + refill;
			const double audio_true = (step_time - 0.45) * (1.0 + sc.drift_ppm * 1e-6) + k_period + sc.lead0;
			const int audio_ms = static_cast<int>(audio_true + noise + 0.1);

			const int32_t target_in = target;
			if (ms_clock && (f & 1) == parity)
				target += extra_ticks(audio_ms, target, 0, k_fps);
			if (target < target_in)
				r.removed += target_in - target;

			tick = target; // process
			check = !check;
			if (check)
			{
				const int c = chart_ms(target, 0, k_fps);
				if (audio_ms > 0 && c > 0 && c + 17 < audio_ms)
					++target;
			}
			++target;
			r.ticks.push_back(tick);
			r.leads.push_back(audio_true - tick * k_period);
		}
		return r;
	}

	int failures = 0;

	void expect(bool ok, const char* what)
	{
		std::printf("    %s: %s\n", ok ? "ok  " : "FAIL", what);
		if (!ok)
			++failures;
	}

	double mean(const std::vector<double>& v, size_t from)
	{
		double s = 0.0;
		for (size_t i = from; i < v.size(); ++i)
			s += v[i];
		return s / static_cast<double>(v.size() - from);
	}

	// frames after 'from' until the chart is back within one game check of the threshold
	int recovery(const run_t& r, size_t from)
	{
		for (size_t i = from; i < r.leads.size(); ++i)
			if (r.leads[i] <= 17.0 + k_period + 1.0)
				return static_cast<int>(i - from);
		return -1;
	}
}

int main()
{
	// 1. normal play: identical to the game, frame for frame
	std::printf("normal play (no stall): identical to the game\n");
	int identical = 0, cases = 0;
	for (const double drift : { 0.0, 100.0, -100.0 })
		for (double lead = 0.0; lead <= 16.0; lead += 1.0)
			for (unsigned seed = 1; seed <= 3; ++seed)
				for (int parity = 0; parity < 2; ++parity)
				{
					const scenario_t sc{ "", lead, drift, 1.0, 0.0, 0.0, 0, 0.0 };
					const auto g = run(sc, false, seed, parity);
					const auto o = run(sc, true, seed, parity);
					++cases;
					identical += g.ticks == o.ticks && o.removed == 0;
				}
	std::printf("    %d of %d runs identical (start lead 0-16 ms, drift 0 / +-100 ppm, audio +-1 ms)\n", identical, cases);
	expect(identical == cases, "every step identical to the game");

	// 2. disturbed play: same mean phase as the game
	const scenario_t disturbed[] = {
		{ "start lead 26 ms (two game catch-ups)", 26.0, 0.0, 1.0, 0.0, 0.0, 0, 0.0 },
		{ "start lead 40 ms", 40.0, 0.0, 1.0, 0.0, 0.0, 0, 0.0 },
		{ "absorbed stalls 1-15 ms (1 %), audio +-3 ms", 10.0, 0.0, 3.0, 0.01, 15.0, 0, 0.0 },
		{ "absorbed stalls 1-30 ms (0.3 %), drift +100 ppm", 8.0, 100.0, 2.0, 0.003, 30.0, 0, 0.0 },
	};
	for (const auto& sc : disturbed)
	{
		std::printf("%s\n", sc.name);
		double dg = 0.0, dsum = 0.0, max_abs = 0.0;
		int removed = 0;
		const int seeds = 20;
		for (unsigned seed = 1; seed <= seeds; ++seed)
		{
			const auto g = run(sc, false, seed, 0);
			const auto o = run(sc, true, seed, 0);
			const double d = mean(o.leads, 240) - mean(g.leads, 240);
			dsum += d;
			dg += mean(g.leads, 240);
			max_abs = std::max(max_abs, std::fabs(d));
			removed += o.removed;
		}
		std::printf("    mean lead: game %.2f ms, MS CLOCK differs by %+.3f ms on average (largest single song %.2f ms)\n",
			dg / seeds, dsum / seeds, max_abs);
		expect(std::fabs(dsum / seeds) < 0.5, "same mean phase as the game");
		expect(removed == 0, "no game tick removed");
	}

	// 3. long stalls that lose frames: same phase afterwards, reached sooner
	for (const double big : { 80.0, 200.0, 600.0 })
	{
		char name[64];
		std::snprintf(name, sizeof(name), "one %.0f ms stall that loses frames", big);
		std::printf("%s\n", name);
		const scenario_t sc{ name, 10.0, 0.0, 1.0, 0.0, 0.0, 1200, big };
		int rg = 0, ro = 0;
		double dsum = 0.0;
		const int seeds = 20;
		for (unsigned seed = 1; seed <= seeds; ++seed)
		{
			const auto g = run(sc, false, seed, 0);
			const auto o = run(sc, true, seed, 0);
			rg += recovery(g, 1199);
			ro += recovery(o, 1199);
			dsum += mean(o.leads, 1200 + 300) - mean(g.leads, 1200 + 300);
		}
		std::printf("    frames out of sync: game %d, MS CLOCK %d; phase afterwards differs by %+.3f ms\n", rg / seeds, ro / seeds, dsum / seeds);
		expect(std::fabs(dsum / seeds) < 0.5, "same phase as the game afterwards");
		expect(ro <= rg, "back in sync no later than the game");
	}

	std::printf(failures ? "timing_logic_test: %d FAILED\n" : "timing_logic_test: all checks passed\n", failures);
	return failures ? 1 : 0;
}
