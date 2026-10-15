# IIDX Toolbox

为街机版 IIDX提供额外功能。

## 安装与启动

- **启动方式：** 使用 `spice64.exe -k iidx_toolbox.dll` 挂载启动。
- **配置文件：** 自动保存在同目录的 `iidx_toolbox.ini`，日志为 `iidx_toolbox.log`（特征码不匹配的功能会自动跳过，不会导致游戏崩溃）。

## 功能列表（默认按 Insert 呼出鼠标菜单，关闭自动保存）

### 自定义乱（Random Fix）

- **固定排列：** 自由指定 RANDOM 的键位顺序（1P/2P 独立设置）。
- **乱 按钮：** 每局重新随机抽一次排列。
- **组 按钮：** 同色乱（白键只在白键间乱，黑键只在黑键间乱）。

注：使用轨道票时会自动停止

### 特殊模式（ALL SCRATCH / ALL CHARGE）

强制全皿 / 全长押。开启后成绩强制算作ASSIST CLEAR。

### 特效透明度（Opacity）

打击爆炸（Bombs）、按键光束（Key Beams）、判定文字与连击数，均可独立调节透明度（75% / 50% / 25% / 0% 或手动输入），下一首生效。

### 判定视觉（PGREAT BLUE / NO BLINK）

- **PGREAT BLUE：** PGREAT固定显示为蓝色，不再闪烁。
- **NO BLINK：** GREAT 至 POOR 判定文字常亮显示。

### 高精度帧同步（MS CLOCK）

谱面滚动直接锚定音频位置与垂直同步（VSync），消除 CPU 波动导致的视觉卡顿、掉帧追赶和音画慢漂移（仅在标准 60Hz LDJ / 120Hz TDJ 下生效）。

### 系统性能优化（System）

渲染线程启用 Windows 游戏多媒体调度（MMCSS），关闭进程电源节流，可在配置中自定义进程优先级。

---

# IIDX Toolbox

A spice2x enhancement plugin for arcade IIDX. It provides an in-game menu for custom functions.

## Setup & Usage

- **Load:** Run `spice64.exe -k iidx_toolbox.dll`.
- **Config & Logs:** Saved to `iidx_toolbox.ini` and `iidx_toolbox.log` in the DLL folder. (Unsupported features on mismatched game versions are safely skipped).

## Features (Press Insert for mouse menu; saves on close)

### Random Fix (Per-side)

- **Fixed Order:** Lock RANDOM to a custom lane order.
- **ran:** Roll a new random order each play.
- **group:** Shuffle white and black keys strictly within their own color groups.

Note: Automatically bypassed when a Lane Ticket is used.

### Chart Mods (ALL SCRATCH / ALL CHARGE)

Converts charts to all scratches or all charge notes. Marks play as ASSIST CLEAR (unranked).

### Opacity

Set transparency (75% / 50% / 25% / 0% or custom) for Bombs, Key Beams, and Judgement/Combo text. Applies on the next stage.

### Visual Tweaks

- **PGREAT BLUE:** Locks PGREAT to static blue instead of cycling colors.
- **NO BLINK:** Disables the 12 Hz blinking on GREAT/GOOD/BAD/POOR judgements.

### MS CLOCK

Syncs chart scrolling directly to VSync and audio position instead of frame counting. Eliminates CPU micro-stutter, catches up missed VSyncs, and prevents audio drift (Active at native 60Hz LDJ / 120Hz TDJ).

### System Boost

Runs render thread under MMCSS "Games" priority, disables Windows power throttling, and supports custom process priority in .ini.
