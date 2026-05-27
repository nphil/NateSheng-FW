# CLAUDE.md — UV-K5 firmware study notes

The user has a classic **Quansheng UV-K5** (DP32G030 MCU) and wants to build
their own optimized firmware for it. Focus: **performance, UX, sensible features**,
plus a back-port of the **FSK text messenger** subsystem from a sister fork.

**This repo is now the user's UV-K5 fork.** Layout:

| Path | What it is |
|------|------------|
| Repo root (`./`) | **F4HWN UV-K5 firmware source** — the active build target. `make` here produces `firmware.bin` + `firmware.packed.bin`. Forked at F4HWN v4.3 on EGZUMER v0.22 on DualTachyon's open base |
| `./reference/k1-gogufw/` | **GOGUFW 0.3.12 for UV-K1 (PY32F071)** — preserved as the **source of truth for the FSK messenger subsystem** we'll back-port. NOT built as part of the K5 firmware. Look here for `App/app/messenger*.c` |

Lineage:

    DualTachyon UV-K5 base  →  EGZUMER  →  F4HWN ─┬─→  (UV-K5 line) — what we want
                                                  └─→  F4HWN K1 port  →  GOGUFW (messenger added)

---

## 1. Hardware target — UV-K5

| Item | Value |
|------|-------|
| MCU | **DP32G030** (ARM Cortex-M0, ~48 MHz) |
| Flash | **64 KB total**; firmware lives in **60 KB at 0x00000000** (no bootloader offset — the custom FW *replaces* Quansheng's loader) |
| RAM | **16 KB at 0x20000000** (heap = 0, stack min 0x80 — really tight) |
| Radio IC | **BK4819** (VHF/UHF) — same chip as K1, same driver |
| FM tuner | **BK1080** (optional, `ENABLE_FMRADIO`) — same as K1 |
| LCD | **ST7565** 128×64 mono, SPI, single 1024 B framebuffer — same as K1 |
| **Persistent store** | **I²C 24C64-style EEPROM, 8 KB** at slave addr `0xA0`, 16-bit register address, 8-byte page writes. Used for everything: settings, channels, DTMF, calibration. (`driver/eeprom.c:25`) |
| UART | **Yes**, used both for the CPS protocol and for flashing via `k5prog` |
| USB | **No** — DP32G030 has no USB peripheral. Anything that needs PC-side comms goes over UART |
| Bootloader | None after flashing custom FW. Recovery = re-flash over UART with PTT+SIDE1 held to enter the stock loader (which can also accept `.packed.bin` from the stock Quansheng updater) |

Linker: `firmware.ld` (60 KB FLASH @ 0x00000000, 16 KB RAM @ 0x20000000).
Startup: `start.S` (253 LOC).
Init: `init.c` (BSS_Init + DATA_Init).
Vendor MCU defines: `bsp/dp32g030/*.h` (generated from `hardware/dp32g030/*.def`).

**Compared to UV-K1**: ~half the flash (60 KB vs 118 KB), same RAM, no USB, smaller
EEPROM (8 KB I²C vs 2 MB QSPI on K1). The K1 has all the room in the world; the
K5 is tight. **Every feature added costs visible code space.**

---

## 2. Build system

**Plain Makefile**, `arm-none-eabi-gcc`, ~600 LOC.

```bash
./compile-with-docker.sh           # uv-k5-f4hwn/Dockerfile drives this
# or, with toolchain on host:
make                               # produces f4hwn, f4hwn.bin, f4hwn.packed.bin
```

Important compile flags (`Makefile:283`):

- `-Oz` — aggressive size-optimization, more so than `-Os`. Don't downgrade.
- `-fshort-enums`, `-fno-delete-null-pointer-checks`, `-std=c2x`
- `-Wall -Werror -Wextra` — warnings are errors.
- `ENABLE_LTO ?= 1` (default ON, K5 needs it).
- `ENABLE_EXPERIMENTAL_CLFAGS ?= 1` adds `-funroll-loops -ffat-lto-objects`.
- `--specs=nano.specs` (newlib-nano).

**`ENABLE_OVERLAY` vs `ENABLE_LTO` are mutually exclusive** (Makefile:97):
- `OVERLAY=1` compiles in `sram-overlay.c` + `driver/flash.c` so the FW can
  self-reflash over UART/AirCopy. Code runs from RAM while erasing flash.
- `LTO=1` shrinks the binary but breaks the SRAM-section placement.
- Practical choice: leave LTO on; only flip to OVERLAY for "self-updating" builds.

**`fw-pack.py`** wraps the raw `.bin` into the format the **stock Quansheng updater
accepts** — XOR scramble + version/author footer + CRC. This is what produces
`f4hwn.packed.bin`. Needs Python + `crcmod` on the build host (Makefile:539).

**No CMake**, no presets file. Build matrix is just Makefile `?=` defaults at the
top, overridable on the command line: `make ENABLE_FMRADIO=1 ENABLE_AIRCOPY=1`.

Debug/flash via OpenOCD + JLink (`Makefile:564`, uses `dp32g030.cfg`):
```bash
make debug    # openocd server
make flash    # write firmware.bin via SWD
```

There is **no GitHub Actions CI** in this repo — releases are just `.packed.bin`
uploads.

---

## 3. Source layout

**Files live in the repo root**, not under `App/` like the K1 fork. No
`Drivers/`, `Middlewares/`, `Core/` — those K1 dirs hold the PY32F071 HAL,
which K5 doesn't need.

```
uv-k5-f4hwn/
├── start.S              # Cortex-M0 startup, vector table, copies .data
├── init.c               # BSS_Init + DATA_Init called from start.S
├── firmware.ld          # Linker script (60 KB FLASH, 16 KB RAM)
├── main.c               # Main() — boot init, while-true loop
├── scheduler.c          # SysTick handler — 10 ms heartbeat
├── radio.c              # VFO config, TX/RX register setup
├── settings.c           # EEPROM persistence (channels, config)
├── functions.c          # FUNCTION_* state machine
├── audio.c              # DAC/mic routing, beep queue
├── frequencies.c        # Band tables, step sizes, F-lock regions
├── am_fix.c             # Per-frequency AGC gain LUT for AM
├── dcs.c                # CTCSS / CDCSS tables
├── font.c, bitmaps.c    # Rasters (font.c is the biggest rodata blob)
├── misc.c               # Globals, helpers
├── board.c              # GPIO/clock init
├── screenshot.c         # F4HWN: dump LCD over UART (debug)
├── sram-overlay.c       # Flash self-reprogramming, runs from SRAM
├── version.c            # Build metadata
│
├── app/                 # State machines & user-mode apps
│   ├── app.c            # APP_Update(), APP_TimeSlice10ms/500ms()  ★ central loop
│   ├── main.c           # Main VFO screen key handler
│   ├── menu.c           # Settings menu tree (god-file)
│   ├── action.c         # Programmable side-key actions
│   ├── scanner.c        # Channel/freq scanner
│   ├── chFrScanner.c    # F4HWN extended scanner
│   ├── spectrum.c       # Spectrum analyzer
│   ├── fm.c             # BK1080 FM radio mode
│   ├── dtmf.c           # DTMF encode/decode + calling
│   ├── flashlight.c     # Torch LED
│   ├── aircopy.c        # Cloning over FSK   ★ The FSK plumbing the messenger will ride on
│   ├── breakout.c       # Easter-egg game
│   ├── rega.c           # Contrib alarm
│   ├── uart.c           # CPS/serial protocol
│   ├── generic.c, common.c
│   └── (NO messenger* — that's GOGUFW only, must be back-ported)
│
├── driver/              # Hardware abstraction
│   ├── bk4819.c         # Main radio chip — modulation, squelch, CTCSS/DCS, FSK
│   ├── bk1080.c         # FM tuner (optional)
│   ├── st7565.c         # LCD SPI
│   ├── eeprom.c         # I²C 24Cxx (8 KB, 8-byte pages, read-before-write skip)
│   ├── i2c.c            # Bit-banged I²C
│   ├── spi.c            # Bit-banged SPI (for BK4819 + LCD)
│   ├── flash.c          # Internal MCU flash (only built with OVERLAY=1)
│   ├── aes.c            # AES used for the UART CPS auth handshake
│   ├── crc.c            # CRC for aircopy/uart
│   ├── keyboard.c, backlight.c, adc.c, systick.c, system.c, gpio.c, uart.c
│   └── bk4819-regs.h, bk1080-regs.h
│
├── ui/                  # Screen rendering on the framebuffer
│   ├── ui.c             # Display-mode dispatch (DISPLAY_MAIN, _MENU, ...)
│   ├── main.c, menu.c   # Big screens
│   ├── status.c, helper.c, inputbox.c, scanner.c, welcome.c
│   ├── battery.c, fmradio.c, aircopy.c, lock.c
│
├── helper/              # battery.c (smoothing), boot.c (boot-mode detect)
├── external/printf/     # nanoprintf
├── external/CMSIS_5/    # Vendor CMSIS — unlike K1 fork, K5 actually uses these (ARMCM0)
├── bsp/dp32g030/        # MCU register defs (generated from hardware/*.def)
├── hardware/dp32g030/   # Source .def files for the register defs
├── archive/             # Old prebuilt binaries
├── images/, photos/     # Docs / wiki assets
├── k5viewer/            # Optional desktop viewer for screenshots
├── utils/               # Misc tooling
└── fw-pack.py           # Post-build packer (produces .packed.bin)
```

**Per-source line counts** (the god-files to be aware of):

```
app/spectrum.c   ~2.0k    Spectrum analyzer
app/menu.c       ~2.4k    Settings menu (with-handlers)
ui/main.c        ~2.0k    Main VFO render
app/app.c        ~2.0k    Central app loop
settings.c       ~1.4k    EEPROM load/save
driver/bk4819.c  ~1.8k    Radio chip
font.c           ~600     But ~40+ KB of rodata
```

---

## 4. Scheduling model — same as K1

Cooperative, driven by a 10 ms SysTick. ISR sets flags; main loop drains them.
No RTOS, no preemption, no `malloc` after init.

```
SysTick_Handler (scheduler.c, every 10 ms):
    gNextTimeslice = true
    every 50 ticks (500 ms):  gNextTimeslice_500ms = true
    decrement: TX timeout, dual-watch, scan pause, power-save, NOAA, VOX, voice queue, etc.

main.c main loop:
    while (true) {
        APP_Update();                # key poll, UART, scanner/FM tick, RX/TX FSM
        if (gNextTimeslice)   APP_TimeSlice10ms();
        if (gNextTimeslice_500ms) APP_TimeSlice500ms();
    }
```

Six-state radio FSM in `functions.c` / `radio.c`:

    FUNCTION_FOREGROUND  ⇄  FUNCTION_RECEIVE  ⇄  FUNCTION_INCOMING
                         ⇄  FUNCTION_MONITOR  ⇄  FUNCTION_TRANSMIT
                         ⇄  FUNCTION_POWER_SAVE

**Rule of thumb**: ISR only decrements counters and sets flags. All real work
(SPI to BK4819, LCD, I²C to EEPROM) belongs in `APP_TimeSlice*` or `APP_Update`.

---

## 5. Messenger — DOES NOT EXIST in K5 base

The K5 base **has no messenger** — that's GOGUFW's signature K1-only feature.
Back-porting it is the marquee project for this fork. Everything we need to
copy lives in **`/home/user/NateSheng-FW/App/app/messenger*`** (~2500 LOC across
6 files):

- `messenger.c` — UI state machine (HOME / INBOX / OUTBOX / DRAFTS / COMPOSE / READ)
- `messenger_packet.{c,h}` — 94-byte `"GGM1"` wire format, CRC-16 CCITT, TTL hops
- `messenger_store.{c,h}` — inbox(20) / outbox(10) / drafts(8), config struct
- `messenger_rf.{c,h}` — BK4819 FSK TX/RX, ACK + jittered retry, voice-path
  snapshot/restore (1143 LOC — gnarliest file)
- `messenger_t9.{c,h}` — multi-tap text entry
- `messenger_ui.{c,h}` — screen rendering

**Porting strategy & gotchas**:

1. **AIRCOPY dependency**: messenger reuses `g_FSK_Buffer`, `BK4819_SendFSKData`,
   `BK4819_SetupAircopy`. So `ENABLE_AIRCOPY=1` is a hard prerequisite.
2. **Flash budget**: Messenger adds ~12–15 KB code. K5 has 60 KB flash; the F4HWN
   default build leaves <8 KB headroom. **We have to disable other features to
   fit it**. Realistic drop list:
   - `ENABLE_FEAT_F4HWN_GAME` (breakout)
   - `ENABLE_VOICE` (already off in F4HWN default)
   - `ENABLE_NOAA` (US-only, already off)
   - `ENABLE_FMRADIO` (~5 KB; only if user doesn't want FM)
   - `ENABLE_DTMF_CALLING` (heavy and rarely used)
   - `ENABLE_PWRON_PASSWORD`
   - Possibly trim `app/spectrum.c` (~2 KB) or use the smaller F4HWN spectrum
3. **EEPROM storage** for messenger: K1 used a 4 KB sector at `0x012000` in
   a 2 MB QSPI flash. **K5 has only 8 KB total I²C EEPROM**, of which the
   majority is already used by channel memories + VFO settings + DTMF. The
   messenger persistent config (callsign, flags, 8 drafts) is **~360 bytes**
   and *can* fit; we need to find an unused EEPROM range. The K5 EEPROM map
   is documented in `settings.c` — pick a sector that's currently padded or
   "reserved for future use". DO NOT overlap channel memories or the
   24Cxx-compat region the CPS expects.
4. **No persistent inbox/outbox even on K5** (just like K1). Lost on power
   cycle. Improving this is a fork opportunity — see §10.
5. **No USB on K5**: messenger debug/inject-from-PC paths in `messenger_rf.c`
   that assume VCP need to be wired to UART instead, or `#ifdef`-gated out.
6. **`#ifdef ENABLE_MESSENGER`** gate the whole subsystem so the K5 default
   build (without messenger) still compiles.
7. **No `driver/py25q16.c`** on K5. `messenger_store.c` currently calls
   `PY25Q16_ReadBuffer` / `PY25Q16_WriteBuffer` — these need to become
   `EEPROM_ReadBuffer` / `EEPROM_WriteBuffer` (note: 8-byte page granularity!)
8. **`bk4829.c` doesn't exist on K5** — only `bk4819.c`. (The K1 fork has both,
   apparently as near-duplicates; one of the K1's optimization opportunities
   is to dedupe.)
9. **Packet format** is portable as-is. Bumping `MSG_PKT_VERSION` is only
   needed if we change the wire format — keep `1` for K5↔K1 interop.
10. **T9 / UI** code is portable as-is; only the bitmap font dependencies and
    `gFrameBuffer` access need to match the K5 helpers (they're named the same).

---

## 6. Settings & EEPROM map (8 KB I²C 24C64)

K5 stores **everything** persistent in 8 KB of I²C EEPROM (`driver/eeprom.c`).
Read/write semantics:

- I²C slave `0xA0` (24Cxx family).
- 16-bit register address.
- Reads: arbitrary length.
- Writes: **8-byte page granularity** with 8 ms burn delay (`EEPROM_WriteBuffer`).
- Optimization already in place: read-before-write skip if data is unchanged.

Approximate map (from `settings.c` and EGZUMER docs):

| Range | Contents |
|-------|----------|
| `0x0000–0x0DAF` | 200 channels × 16 B per memory channel (MR1–MR200) |
| `0x0DB0–0x0E27` | Per-channel attributes (band, scanlist, etc.) |
| `0x0E40–0x0E70` | VFO settings (frequency, mode, power) for VFO A/B |
| `0x0E70–0x0E78` | DTMF settings + side tones |
| `0x0E78–0x0F18` | Radio config — squelch, TX timeout, dual-watch, scan, etc. |
| `0x0F18–0x0F40` | Power-on password, key actions, brightness |
| `0x0F50–0x0F70` | Battery calibration |
| `0x1000–0x18FF` | 16 named channel groups, scan lists |
| `0x1900–0x1A00` | DTMF contacts |
| `0x1A00–0x1E00` | Custom logo/welcome screen, scan range table |
| `0x1E00–0x1FFF` | Last ~512 B — partly free, partly used by F4HWN extras |

**Where to put messenger config**: a single ~360 B block. The cleanest move is
to carve a fresh range from `0x1E80–0x1FFF` (last 384 B), which various forks
treat as "scratchpad". Verify against `settings.c` before committing.

Auto-save trigger: `gScheduleVfoSave` flag in scheduler.c → write on idle (same
pattern as K1).

---

## 7. BK4819 radio driver

Same chip as the K1, same driver structure (`driver/bk4819.c` ~1.8k LOC). Same
mental model: there's no public datasheet, all knowledge is reverse-engineered.

AF output modes, filter bandwidths (WIDE / NARROW / NARROWER on F4HWN / AM),
CTCSS/CDCSS, FSK — all identical to the K1 driver.

Functions the fork will touch:
- `BK4819_SetFrequency()`
- `BK4819_SetupPowerAmplifier(bias, freq)`
- `BK4819_SetupSquelch()`
- `BK4819_SetCTCSSFrequency()`, `BK4819_SetCDCSSCodeWord()`
- `BK4819_SetAF()` — speaker routing; critical for messenger voice-path restore
- `BK4819_ResetFSK()`, `BK4819_SetupAircopy()`, `BK4819_SendFSKData()` — the FSK
  path the messenger will ride on

**No `bk4829.c` here** (K1 had a suspicious near-duplicate). That's one less
file to study.

`am_fix.c` — linear-scan AGC LUT for AM band, same as K1. Binary-search
opportunity.

---

## 8. UI framework

Same single-framebuffer model as K1:
- `gFrameBuffer[128*8 = 1024]` bytes
- ST7565 stores 8 vertical pixels per byte; `(y >> 3)` selects page row
- Render = redraw the whole screen, blit on `gUpdateDisplay = true`
- No dirty-rect tracking

Fonts (`font.c`) are 5×7 and 8×8 raster glyphs + big-digit set. **The biggest
single chunk of rodata on K5** — pruning unused glyphs or RLE compression is a
real lever on a 60 KB budget.

Display modes (`enum DISPLAY_TYPE` in `ui/ui.h`): `DISPLAY_MAIN`, `_MENU`,
`_SCANNER`, `_FM`, `_AIRCOPY`. When we add messenger we'll add `_MESSENGER`
to that enum.

Status bar (`ui/status.c`) re-runs on every redraw; cheap but constant cost.

---

## 9. Feature-flag matrix

All flags in `Makefile` head (`?= 0` or `?= 1`). The F4HWN K5 defaults:

**Stock Quansheng features (defaults)**:
- ON: `UART`, `TX1750`, `VOX`, `FLASHLIGHT`
- OFF: `FMRADIO`, `AIRCOPY`, `NOAA`, `VOICE`, `ALARM`, `PWRON_PASSWORD`,
  `DTMF_CALLING`

**Custom mods (EGZUMER lineage, defaults)**:
- ON: `BIG_FREQ`, `SMALL_BOLD`, `CUSTOM_MENU_LAYOUT`, `KEEP_MEM_NAME`,
  `WIDE_RX`, `NO_CODE_SCAN_TIMEOUT`, `AM_FIX`, `SQUELCH_MORE_SENSITIVE`,
  `FASTER_CHANNEL_SCAN`, `RSSI_BAR`, `AUDIO_BAR`, `COPY_CHAN_TO_VFO`,
  `SCAN_RANGES`
- OFF: `SPECTRUM` (overridden by F4HWN_SPECTRUM), `TX_WHEN_AM`, `F_CAL_MENU`,
  `CTCSS_TAIL_PHASE_SHIFT`, `BOOT_BEEPS`, `SHOW_CHARGE_LEVEL`,
  `REVERSE_BAT_SYMBOL`, `REDUCE_LOW_MID_TX_POWER`, `BYP_RAW_DEMODULATORS`,
  `BLMIN_TMP_OFF`

**F4HWN pack (defaults)**:
- ON: `FEAT_F4HWN`, `FEAT_F4HWN_SPECTRUM`, `FEAT_F4HWN_RX_TX_TIMER`,
  `FEAT_F4HWN_SLEEP`, `FEAT_F4HWN_RESUME_STATE`, `FEAT_F4HWN_NARROWER`,
  `FEAT_F4HWN_INV`, `FEAT_F4HWN_CTR`, `FEAT_F4HWN_CA`
- OFF: `FEAT_F4HWN_GAME`, `FEAT_F4HWN_SCREENSHOT`, `FEAT_F4HWN_CHARGING_C`,
  `FEAT_F4HWN_RESCUE_OPS`, `FEAT_F4HWN_VOL`, `FEAT_F4HWN_RESET_CHANNEL`,
  `FEAT_F4HWN_PMR`, `FEAT_F4HWN_GMRS_FRS_MURS`, `FEAT_F4HWN_DEBUG`

**What's MISSING vs K1 GOGUFW** (so we can't blindly cherry-pick):
- `ENABLE_MESSENGER` and the 6 messenger files (the back-port project)
- `ENABLE_USB` and `usb/` dir (no USB peripheral on DP32G030)
- `ENABLE_FEAT_F4HWN_BEAM`, `_QRCODE`, `_LOGO`, `_MEM`, `_AUDIO`,
  `_AUDIO_SCOPE`, `_SCAN_PROGRESS`, `_SCAN_FASTER`, `_RESET_VFO` —
  these are newer K1-port additions
- `driver/bk4829.c`, `driver/py25q16.c`, `driver/vcp.c`, `driver/voice.c` (last
  only present here if `ENABLE_VOICE`)

**Compiler/linker flags worth knowing**:
- `ENABLE_CLANG`, `ENABLE_SWD`, `ENABLE_OVERLAY`, `ENABLE_LTO`,
  `ENABLE_EXPERIMENTAL_CLFAGS`. The default combo (`LTO=1`,
  `EXPERIMENTAL_CLFAGS=1`, `OVERLAY=0`) is sensible — keep it.

---

## 10. Opportunities for our optimized K5 fork

Priority ranked for "perf + UX + sensible features" against a tight 60 KB
budget:

### Flash space (the binding constraint)

1. **Aggressive feature trim**: For our build, default-OFF anything we don't
   personally use (game, screenshot, voice, NOAA, DTMF calling, F-cal menu,
   pwron password). Each saves 0.5–3 KB.
2. **Font pruning**: `font.c` is ~40 KB of rodata. We use ASCII subset plus
   a handful of symbols. Strip unused glyphs → likely 10+ KB saved.
3. **Dedupe duplicated rodata** — the F4HWN menu strings, the spectrum text
   labels, etc. are duplicated across modules.
4. **AM-fix LUT → binary search**, cache last-found index.
5. **Drop `external/CMSIS_5/` headers we don't use** — only `ARMCM0` is needed.
6. **Coalesce 10 ms ISR work** — some decrements only need 500 ms cadence.

### Code health

7. **Top-level god files** — `app/menu.c` (~2.4k), `app/app.c` (~2.0k),
   `ui/main.c` (~2.0k), `app/spectrum.c` (~2.0k), `settings.c` (~1.4k).
   Extract logically grouped subsections into separate `.c` files. Helps
   compile speed and (with LTO + `-ffunction-sections`) helps dead-code
   elimination too.

### UX

8. **Bring in the messenger** (§5). The marquee feature.
9. **Persistent inbox/outbox** (improves on K1): even one or two saved
   messages in EEPROM is more useful than zero. Need a tiny ring buffer
   inside the messenger EEPROM block, page-aligned.
10. **Dirty-rect rendering** for the LCD — status bar updates twice a second
    currently redraw the whole 1024 B framebuffer. Worth it on a slow SPI bus.
11. **BK4819 register write cache** — skip SPI when the value didn't change.
12. **Hidden calibration menu exposure** for `BK4819_XTAL_FREQ_LOW` (ppm
    trim) — currently behind F-lock boot mode. Promote it.
13. **Cleaner TX timeout countdown UX** — the current alert is beep+blink
    loop; on-screen "TX TOT in 5s" countdown is friendlier.

### Build/distribution

14. **Add a GitHub Actions workflow** to build on push (this repo has none —
    the K1 fork has a stale one we can crib from).
15. **Keep `fw-pack.py`** wired in (it already is here — unlike the K1
    fork where it's commented out). This means our K5 release will be
    flashable with the **stock Quansheng updater**, which is a real UX win.
16. **Document a `make` cheatsheet** in the README — feature-flag combos for
    common build profiles (Minimal, Messenger, Full).

---

## 11. Conventions to follow

- **Tabs are 4 spaces.**
- **C2x** (`-std=c2x`), `-Wall -Werror -Wextra`.
- Globals are `g*Camel` (DualTachyon style); file-static state is `s_snake_case`
  (messenger module style).
- Feature gating is **always** `#ifdef ENABLE_FOO`. Wire new flags into both
  `Makefile` (the `?=` block at the top, the `OBJS +=` block, and the
  `CFLAGS +=` block) — three places, easy to forget one.
- New `.c` files go under the matching subdir (`app/`, `driver/`, `ui/`,
  `helper/`) and must be listed in `Makefile`.
- BK4819 register access happens from the foreground (main loop), never from
  the SysTick ISR.
- No `malloc` after init. All buffers are static or stack.
- EEPROM writes are 8 B page-aligned. `EEPROM_WriteBuffer()` already
  skips writes if the data is unchanged — don't bypass that.
- Don't break the messenger's voice-path snapshot/restore around any new RF
  TX paths (when porting): it's load-bearing for "voice doesn't die after
  sending a message."

---

## 12. Git / branch policy

- The study branch `claude/uv-k1-firmware-study-fpSNS` has been **merged into
  `main`** (2026-05-27). All work now happens on `main` directly.
- Feature branches are welcome for larger changes; merge back to `main` when ready.
- Don't open a PR unless explicitly asked.
- The GitHub MCP tools are restricted to `nphil/natesheng-fw` only.

---

## 13. Quick navigation cheat-sheet (K5 paths)

| I want to... | Look here (paths relative to repo root) |
|--------------|-----------------------------------------|
| Change the main loop | `main.c`, `app/app.c` (APP_Update / APP_TimeSlice10ms / APP_TimeSlice500ms) |
| Add an ISR-timed countdown | `scheduler.c` |
| Add a menu item | `app/menu.c`, `ui/menu.c` |
| Add a programmable side-key action | `app/action.c`, `enum ACTION_OPT_t` in `settings.h` |
| Add a new display screen | `ui/ui.c` (dispatch), new `ui/*.c`, new `app/*.c` for state |
| Modify TX/RX behavior | `radio.c`, `functions.c` |
| Read/write EEPROM | `driver/eeprom.c` (`EEPROM_ReadBuffer`, `EEPROM_WriteBuffer`) |
| Change BK4819 modulation | `driver/bk4819.c` + `driver/bk4819-regs.h` |
| Persist a new user setting | `settings.h` (struct field) + `settings.c` (load/save) |
| Add a feature flag | `Makefile` — three places: defaults, `OBJS +=`, `CFLAGS +=` |
| Find the LCD framebuffer | `gFrameBuffer[]`, `driver/st7565.c` |
| Pack a build for the stock updater | `fw-pack.py` (already wired into `make`) |
| Flash via SWD | `make flash` (Makefile:567, uses `dp32g030.cfg`) |

For messenger reference (back-port source — all under `reference/k1-gogufw/`):

| Module | Path |
|--------|------|
| Packet format | `reference/k1-gogufw/App/app/messenger_packet.{c,h}` |
| RF / TX-RX / ACK | `reference/k1-gogufw/App/app/messenger_rf.c` (1143 LOC) |
| Inbox/outbox/drafts | `reference/k1-gogufw/App/app/messenger_store.{c,h}` |
| T9 input | `reference/k1-gogufw/App/app/messenger_t9.{c,h}` |
| UI state machine | `reference/k1-gogufw/App/app/messenger.{c,h}` |
| UI rendering | `reference/k1-gogufw/App/app/messenger_ui.{c,h}` |

---

## 14. K5 vs K1 deltas — quick reference

| Aspect | UV-K5 (target) | UV-K1 / K5V3 (this repo's K1 fork) |
|--------|----------------|------------------------------------|
| MCU | DP32G030 (Cortex-M0) | PY32F071xB (Cortex-M0+) |
| Flash for FW | **60 KB** at 0x00000000 | 118 KB at 0x08002800 (10 KB bootloader) |
| RAM | 16 KB | 16 KB |
| Bootloader | Replaced by FW | Preserved, FW starts above |
| Persistent store | **8 KB I²C 24Cxx EEPROM** | 2 MB SPI NOR (PY25Q16) |
| USB | No (UART only) | Yes (CherryUSB CDC) |
| Build system | Makefile | CMake + Ninja |
| HAL | Hand-rolled register defs in `bsp/dp32g030/` | Puya vendor HAL in `Drivers/PY32F071_HAL_Driver/` |
| Startup | `start.S` + `init.c` | Puya `startup_py32f071xx.s` + `Core/Src/main.c` |
| Linker script | `firmware.ld` | `Core/py32f071xb.ld` |
| Vendor middleware | None | CherryUSB |
| `fw-pack.py` | Wired in (active) | Commented out |
| Messenger | **Not present** | Full subsystem |
| `bk4829.c` | Not present | Present (looks like a `bk4819.c` near-dup) |
| Default optimization | `-Oz`, LTO on | `-Os` (per CMake), LTO **off** by default |
| Self-reflash | `ENABLE_OVERLAY=1` (mutually exclusive with LTO) | Not present (relies on bootloader) |
| File layout root | Files in repo root + `app/`, `driver/`, `ui/`, etc. | Everything under `App/` |

---

_Last updated: 2026-05-27. Study branch merged to `main`. Target: classic UV-K5
(DP32G030). K5 base: `armel/uv-k5-firmware-custom` (F4HWN v4.3 on EGZUMER
v0.22). K1 messenger reference preserved in `reference/k1-gogufw/`._
