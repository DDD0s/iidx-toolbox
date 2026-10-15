# IIDX Toolbox

A spice2x module for the arcade game (bm2dx.dll). Load it with `spice64.exe -k iidx_toolbox.dll`
(or any loader that calls `iidx_toolbox_init`). Settings live in `iidx_toolbox.ini` next to the
DLL; the log is `iidx_toolbox.log` (previous run: `.log.1`).

## Features

| Feature | What it does |
|---|---|
| Menu | Opens with Insert (`[general] MenuKey`). Mouse driven. Settings are saved when it closes. |
| Random Fix | Replaces the lanes of a RANDOM play with a chosen order. Per side: a fixed order, the ran button (new random order every play) or the group button (white and black keys keep their places, numbers shuffled within each colour). Stays out of the way when a lane ticket is armed. |
| ALL SCRATCH / ALL CHARGE | Rewrites the chart at load. The play is reported as an AUTO SCRATCH assist play (ASSIST CLEAR lamp, no ranked score). |
| Opacity | Bombs, key beams and judgement text (with combo), each 75 / 50 / 25 / 0 % or a manual value; nothing selected = 100 % (unchanged). Takes effect from the next stage. |
| PGREAT BLUE | PGREAT stays on the blue variant instead of cycling white/pink/blue. Works for every judgement design. |
| NO BLINK | GREAT/GOOD/BAD/POOR are drawn every frame instead of blinking at 12 Hz. |
| MS CLOCK | The chart follows the vsync on which each frame is shown, anchored to the audio position, instead of counting frames: CPU jitter inside a frame changes nothing, frames that miss vsyncs are caught up in the frame itself (a learned predictor), slow drift is corrected from the audio. Active only while frames come at the timing rate (60 Hz LDJ, 120 Hz TDJ). |
| System | Render thread under MMCSS "Games", Windows power throttling off for the process, optional process priority (`[system]`). |

## Build

`build.cmd` (newest Visual Studio, x64, static CRT, /W4 /WX). Tests: `test\build_test.cmd`, then
`build\test\random_logic_test.exe`, `build\test\timing_logic_test.exe`, and with the game DLL
`build\test\smoke.exe <bm2dx.dll> build\iidx_toolbox.dll` / `render.exe <bm2dx.dll> <dll> <out.bmp>`.

## Game sites (LDJ 2025090300 bm2dx.dll)

Every site is found by a signature that must match exactly once; a component whose sites are
missing logs "not installed" and leaves the game alone.

| Component | Sites |
|---|---|
| random_fix | arrange_apply 0x180742940 (via its call site), option getter 0x1807A46F0, predicates 0x1807B77A0 / 0x1807B79D0 / 0x1807B7920, lane ticket check 0x18058DB00 |
| chart_mod | post chart load 0x180745BE0, assist check 0x18084CB70, reported option bits 0x1804E86A0 |
| effects | effect alpha 0x1807B5BA0 (callers: bombs 0x18081C810, beams 0x180823150, judgement 0x180827760, combo 0x180826DF0), PGREAT colour 0x180827056, blink branch 0x1808270E3 |
| timing | play step 0x180743DE0, chart clock 0x18B2E3B50, audio position 0x1804EA690, audio start tick 0x18073BBB0; vsync grid from IDirect3DSwapChain9Ex::GetPresentStats after each Present |
| d3d (menu, effects, timing, system) | IDirect3D9Ex::CreateDevice(Ex) -> IDirect3DSwapChain9::Present / Reset(Ex) of the first HAL device |

## License

IIDX Toolbox is free software under the GNU General Public License v3.0 (see `LICENSE`).
Vendored libraries keep their own licenses: MinHook (BSD 2-Clause, `vendor/minhook/LICENSE.txt`)
and Dear ImGui (MIT, `vendor/imgui/LICENSE.txt`).
