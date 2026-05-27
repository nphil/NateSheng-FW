# CLAUDE.md — UV-K1 firmware study notes

This is **GOGUFW v0.3.12**, a custom firmware for the **Quansheng UV-K1 / K5V3**
handheld radio, derived from the lineage:

    DualTachyon UV-K5 (open base)  →  EGZUMER  →  F4HWN Fusion  →  GOGUFW

The user is going to **fork this to build their own UV-K1 firmware**, optimized
for **performance, UX, and sensible features**. These notes are the working map.

---

## 1. Hardware target

| Item | Value |
|------|-------|
| MCU | Puya **PY32F071xB** (ARM Cortex-M0+) |
| Flash | 128 KB total; user code = **118 KB** at `0x08002800` (bootloader owns first 10 KB) |
| RAM | **16 KB** at `0x20000000` (heap min 0x200, stack min 0x400) |
| Clock | 48 MHz (set by bootloader, see `Core/Src/main.c:71`) |
| Radio IC | **BK4819** (VHF/UHF transceiver) |
| FM tuner | **BK1080** (optional, `ENABLE_FMRADIO`) |
| LCD | **ST7565** 128×64 mono, SPI, single framebuffer (1024 B) |
| Ext flash | **PY25Q16** SPI NOR — holds settings, channels, messenger config |
| EEPROM compat | Legacy `eeprom_compat.c` shim wraps QSPI flash to look like 24Cxx I²C EEPROM |
| USB | **CherryUSB** CDC (for programming / VCP) |

Linker: `Core/py32f071xb.ld`. Startup: `Core/startup_py32f071xx.s`. Vendor HAL
in `Drivers/PY32F071_HAL_Driver/`. Note the project name in `CMakeLists.txt:12`
is `gogufw`, and the original UV-K5's DP32G030 MCU has been **replaced** with
the PY32F071 on the UV-K1 / K5V3 platform.

---

## 2. Build system

- CMake 3.22+, Ninja generator, `arm-none-eabi-gcc` 13.3.
- Toolchain file: `cmake/gcc-arm-none-eabi.cmake`.
- Presets in `CMakePresets.json`:
  - `default` — hidden base, most features off.
  - `Fusion` — the only public preset; produces `gogufw.elf/.bin/.hex` and
    a `gogufw.map` (also branded "GGFW").
- Build command (Docker, no host toolchain needed):
  ```bash
  ./compile-with-docker.sh        # or ./compile-with-docker.sh Fusion
  ```
  Output: `build/Fusion/gogufw.{elf,bin,hex,map}`.
- VS Code task `GGFW: Build` wraps the same script (see `BUILD_WITH_VSCODE.md`).
- LTO is **off** by default (`ENABLE_LTO: false`); enabling it is a free
  size/perf win to try first.
- `fw-pack.py` (packed/encrypted update format) is **commented out** in
  `CMakeLists.txt:123` — needs reinstating if we want OTA-style upgrade
  packages.
- CI: `.github/workflows/main.yml` just runs the Docker build on push to main
  and uploads the artifact. Currently points at `compiled-firmware/f4hwn.packed.bin`,
  which is stale (path mismatch — first thing to fix in our fork).

---

## 3. Source layout

```
App/                       # All app code lives here
├── main.c                 # Main() entry, init order, while-true loop
├── init.c                 # Stub
├── scheduler.c            # SysTick handler — the heartbeat
├── radio.c                # VFO config, TX/RX register setup, channel load
├── settings.c             # EEPROM/flash persistence (1.4k LOC)
├── functions.c            # FUNCTION_* state machine
├── audio.c                # DAC/mic routing, beep queue
├── frequencies.c          # Band tables, step sizes, F-lock regions
├── am_fix.c               # AM AGC workaround (per-freq gain table)
├── dcs.c                  # CTCSS / CDCSS code tables
├── font.c, bitmaps.c      # Rasters (~50 KB combined — biggest rodata)
├── misc.c                 # Globals, helpers
├── board.c                # GPIO/clock init
├── screenshot.c           # F4HWN: dump LCD over UART
│
├── app/                   # State machines & user-mode apps
│   ├── app.c              # APP_Update(), APP_TimeSlice10ms/500ms()  ★ central loop
│   ├── main.c             # Main VFO screen key handler
│   ├── menu.c             # Settings menu tree (2.4k LOC, the biggest)
│   ├── action.c           # Programmable side-key actions
│   ├── scanner.c          # Channel/freq scanner
│   ├── chFrScanner.c      # F4HWN extended scanner
│   ├── spectrum.c         # Spectrum analyzer (2.6k LOC, biggest single file)
│   ├── fm.c               # BK1080 FM radio mode
│   ├── dtmf.c             # DTMF encode/decode + calling
│   ├── flashlight.c       # Torch LED
│   ├── aircopy.c          # Cloning over FSK   ★ MESSENGER reuses this FSK plumbing
│   ├── beam.c             # F4HWN beacon TX (depends on AIRCOPY)
│   ├── breakout.c         # Easter-egg game
│   ├── rega.c             # Contrib alarm
│   ├── uart.c             # CPS/serial protocol
│   ├── generic.c, common.c, keyboard_state.h
│   └── messenger*.c       # ★ GOGUFW signature, see §5
│
├── driver/                # Hardware abstraction
│   ├── bk4819.c (1.8k)    # Main radio chip — modulation, squelch, CTCSS/DCS
│   ├── bk4829.c (1.9k)    # NOTE: same content as bk4819.c, looks like a near-dup
│   ├── bk1080.c           # FM tuner
│   ├── st7565.c           # LCD SPI
│   ├── py25q16.c          # External SPI flash
│   ├── eeprom_compat.c    # 24Cxx-style API over the QSPI flash
│   ├── keyboard.c, backlight.c, adc.c, systick.c, system.c, gpio.c, spi.c, i2c.c
│   ├── uart.c, vcp.c (USB CDC), voice.c (voice prompts)
│   └── bk4819-regs.h, bk1080-regs.h   # Register maps
│
├── ui/                    # Screen rendering on the framebuffer
│   ├── ui.c               # Display-mode dispatch (DISPLAY_MAIN, _MENU, _MESSENGER, ...)
│   ├── main.c (2.2k)      # Big VFO screen
│   ├── menu.c (1.6k)      # Menu renderer
│   ├── status.c           # Top status bar
│   ├── helper.c           # Text/string blit primitives
│   ├── inputbox.c, scanner.c, welcome.c, battery.c, fmradio.c, aircopy.c, lock.c
│
├── helper/                # battery.c (level smoothing), boot.c (boot-mode detect)
├── external/printf/       # nanoprintf
├── external/CMSIS_5/      # Unused vendor templates (dead weight in tree)
├── usb/                   # CherryUSB descriptors + CDC class
└── CMakeLists.txt         # ★ Feature-flag matrix
```

The full repo also has:

- `Core/` — vendor MCU boot/startup/clock.
- `Drivers/` — PY32F071 HAL and CMSIS.
- `Middlewares/CherryUSB/` — USB stack.
- `archive/` — old prebuilt `.bin`s for K1/K5V3 + stock Quansheng firmware.
- `tools/chirp/` — CHIRP driver (`ggfw_f4hwn_fusion_chirp_v5_5_0_base.py`)
  for channel programming from a PC.

---

## 4. Scheduling model — IMPORTANT

**Cooperative**, driven by a 10 ms SysTick interrupt. The ISR sets flags; the
main loop processes them. No preemption, no RTOS, no malloc-after-init.

```
SysTick_Handler (scheduler.c:48, every 10 ms):
    gNextTimeslice = true
    every 5th tick (50 ms?):  gNextTimeslice40ms = true       # actually every 4 ticks
    every 50 ticks (500 ms):  gNextTimeslice_500ms = true
    decrement: TX timeout, dual-watch, scan pause, power-save, NOAA, VOX, voice queue, etc.

main.c:310 main loop:
    while (true) {
        APP_Update();                # key poll, USB CDC, scanner/FM/messenger tick, RX/TX FSM
        if (gNextTimeslice)   APP_TimeSlice10ms();
        if (gNextTimeslice_500ms) APP_TimeSlice500ms();
    }
```

The 6-state radio FSM lives in `functions.c` / `radio.c`:

    FUNCTION_FOREGROUND  ⇄  FUNCTION_RECEIVE  ⇄  FUNCTION_INCOMING
                         ⇄  FUNCTION_MONITOR  ⇄  FUNCTION_TRANSMIT
                         ⇄  FUNCTION_POWER_SAVE

Rule of thumb when modifying: **don't do anything in the ISR beyond decrementing
counters and setting flags.** All actual work belongs in `APP_TimeSlice*`. SPI
(BK4819, LCD, flash) is unsafe to touch from interrupt context.

---

## 5. Messenger subsystem (GOGUFW's signature feature)

Six files, ~2500 LOC, all in `App/app/messenger*`.

**Packet format** — fixed-width 94 bytes, `messenger_packet.h`:

| Offset | Field | Notes |
|--------|-------|-------|
| 0–3 | Magic `"GGM1"` | `MSG_PKT_MAGIC0..3` |
| 4 | Version | 1 |
| 5 | Type | 1=TEXT, 2=ACK, 3=PING, 4=PONG |
| 6 | Flags | reserved (unicast/encryption/priority planned) |
| 7–8 | ID (LE) | per-message, increments in flash-stored counter |
| 9–10 | TTL init / remain | hop count, for future mesh relay |
| 11–18 | from (8 bytes, null-padded) | callsign |
| 19–26 | to (8 bytes) | `"ALL"` = broadcast |
| 27 | payload_len | ≤ 36 |
| 28–91 | payload | 36 chars usable |
| 92–93 | CRC-16 CCITT (poly 0x1021, init 0xFFFF) | over the first 92 bytes |

ACK packets carry the original message id in both header `id` and `payload[0..1]`
for robust matching (see `messenger_packet.c:80` for the "RF21" mirror).

**RF layer** (`messenger_rf.c`, 1143 LOC — the gnarliest file in the project):

- Reuses the existing **AirCopy** FSK plumbing (`g_FSK_Buffer`, `BK4819_SetupAircopy`,
  `BK4819_SendFSKData`). That's why `ENABLE_MESSENGER` implicitly needs the
  AirCopy infrastructure compiled in.
- Wraps every payload word in `0xABCD ... 0xDCBA` sentinels and adds its own CRC
  (in addition to whatever Aircopy uses).
- **NARROW bandwidth lock** while TX/RX is active; restores user setting after.
- **Voice-path snapshot/restore**: takes a register snapshot before each FSK TX,
  restores it after — the chunk of code calling itself the "8G-style hard
  restore" — to avoid breaking the speaker AF path.
- **Long preamble** (16 bytes via REG_59 `<7:4>=0xF`) on TX so receivers wake
  in time without sending a duplicate packet.
- **ACK + retry**: configurable ACK with **randomized jitter** (`MSG_RF_ACK_SEND_DELAY_MIN_TICKS`
  = 800 ms min, ±300 ms jitter) to reduce collisions across multiple radios.
  Retry timeout is **4 seconds** (`MSG_RF_ACK_TIMEOUT_TICKS = 400`), 1 retry.
- **Duplicate suppression**: `MSG_STORE_IsDuplicateInbox(from, id)` filters
  repeats so a retransmission doesn't double-add to the inbox or re-beep.
- **Boot-time RF prime** so messages can be received before the user ever opens
  the messenger UI.
- A lot of `s_dbg_*` counters and `MSG_RF_GetDbg*()` getters exist for on-screen
  diagnostics — useful when debugging the FSK path.

**Storage** (`messenger_store.c`):

- Inbox = **20**, outbox = **10**, drafts = **8**.
- Each `MSG_Message_t` ≈ 70 bytes → inbox+outbox ≈ 2.1 KB RAM.
- Config (`MSG_Config_t`) lives at `MSG_CFG_FLASH_ADDR = 0x012000` in the
  external **PY25Q16** (a 4 KB sector reserved). **Do not** overlap this with
  the legacy 0x1E80 EEPROM-compat region — there's an explicit comment about it.
- Persists: callsign (6 char, A–Z), RX/ACK/hop/beep/LED/debug toggles, next
  message id, the 8 draft strings.
- Inbox & outbox themselves live in RAM only — **lost on power-cycle**.
  This is a candidate to fix in our fork.

**T9 input** (`messenger_t9.c`):

- 3 modes cycled with `*`: upper letters, lower letters, digits.
- Multi-tap with **800 ms commit timeout** (`MSG_T9_COMMIT_TICKS = 80` ticks).
- Long-press a digit key in letter mode → inserts the digit directly.
- `F` / `EXIT` = backspace.

**UI** (`messenger.c` + `messenger_ui.c`):

- Screens: HOME (4-icon menu), INBOX, OUTBOX (Sent), DRAFTS, COMPOSE, READ.
- HOME icon nav: Inbox / Pencil / Upload arrow / Floppy.
- Opened with **F + MENU** from the main screen.

---

## 6. Settings & flash map

Two storage tiers:

1. **In-RAM mirror** — `gEeprom` (`EEPROM_Config_t`, ~500 bytes), loaded by
   `SETTINGS_InitEEPROM()` at boot.
2. **PY25Q16 SPI flash** (acts as both "EEPROM" for the radio and as messenger
   storage).

Known flash addresses (relative to PY25Q16):

| Addr | Size | Purpose |
|------|------|---------|
| `0x00A000` | 8 B | Config byte block (KEY_LOCK, MENU_LOCK, SET_KEY bits) |
| `0x00A0C8` | 32 B | Custom boot logo lines (2 × 16 B) |
| `0x00A158` | 8 B | Display config (SET_INV bit) |
| `0x00A160` | 16 B | **Stored firmware version string** — used by `SETTINGS_InitEEPROM()` to detect FW upgrade and reset sensitive bits |
| `0x012000` | 4 KB | **Messenger config sector** (callsign, flags, drafts, next_msg_id) |

There's also a 24Cxx-compatible address space used by older Quansheng channel
formats, served by `eeprom_compat.c`. **Be careful**: messenger storage was
moved off `0x1E80` because it collided with MR channel memory there.

Auto-save trigger: `gScheduleVfoSave` flag in scheduler.c → write on idle.

---

## 7. BK4819 radio driver notes

`App/driver/bk4819.c` (1852 LOC). The mental model is "register poke and
hope" — there is no public datasheet, everything is reverse-engineered.

AF output modes (`enum BK4819_AF_Type_t`):

    MUTE / FM / ALAM / BEEP / BASEBAND1 (raw) / BASEBAND2 (USB-like) /
    CTCO / AM / FSKO / + 7 mystery values

Filter bandwidths: WIDE / NARROW / NARROWER (F4HWN add) / AM.

Key things our fork will need to touch:

- `BK4819_SetFrequency()` — tunes RX or TX (separate paths).
- `BK4819_SetupPowerAmplifier(bias, freq)` — PA bias per band; calibration
  table in `gCalibration`.
- `BK4819_SetupSquelch()` — RSSI open/close + noise open/close + glitch
  thresholds. Hysteresis-by-tuning.
- `BK4819_SetCTCSSFrequency()` / `BK4819_SetCDCSSCodeWord()`.
- `BK4819_SetAF()` — speaker routing; critical for both voice and messenger.
- `BK4819_ResetFSK()` / `BK4819_SetupAircopy()` — the FSK path the messenger
  rides on.

⚠ **`bk4829.c` is suspicious** — same size as `bk4819.c`, looks like a
near-duplicate, both are listed in `App/CMakeLists.txt`. Worth investigating
whether one is dead code (size win) or whether they're genuinely two chip
variants.

AM demod uses `am_fix.c` — a per-frequency AGC gain LUT that compensates for
BK4819 AM saturation. Linear scan, could be a binary search.

---

## 8. UI framework

- **Single framebuffer**, `gFrameBuffer[128*8 = 1024]`. ST7565 stores 8 vertical
  pixels per byte, so `(y >> 3)` selects the page row.
- Render = redraw the whole screen, blit on `gUpdateDisplay = true`. No
  dirty-rect tracking. This is a clear performance lever for our fork.
- Fonts (`font.c`) hold 5×7 and 8×8 raster glyphs plus a big-digit set for
  the frequency display. **~46 KB** — the single biggest chunk of read-only
  data and our biggest size lever for flash savings.
- Display modes (`enum DISPLAY_TYPE`): `DISPLAY_MAIN`, `_MENU`, `_SCANNER`,
  `_FM`, `_AIRCOPY`, `_MESSENGER`, etc. Switched by setting
  `gRequestDisplayScreen` and letting `GUI_SelectNextDisplay()` arbitrate.
- Status bar (`ui/status.c`) re-runs on every redraw; cheap but constant cost.

---

## 9. Feature-flag matrix

Defined in `App/CMakeLists.txt:56` via `enable_feature(NAME [files...])`. The
Fusion preset turns nearly everything on. Notable groups:

**Stock Quansheng features**: `FMRADIO`, `AIRCOPY`, `NOAA`, `VOICE`, `VOX`,
`ALARM`, `TX1750`, `PWRON_PASSWORD`, `DTMF_CALLING`, `FLASHLIGHT`, `MESSENGER`
(GOGUFW addition).

**Custom mods** (EGZUMER lineage): `SPECTRUM`, `BIG_FREQ`, `SMALL_BOLD`,
`CUSTOM_MENU_LAYOUT`, `KEEP_MEM_NAME`, `WIDE_RX`, `TX_WHEN_AM`, `F_CAL_MENU`,
`CTCSS_TAIL_PHASE_SHIFT`, `BOOT_BEEPS`, `SHOW_CHARGE_LEVEL`,
`REVERSE_BAT_SYMBOL`, `NO_CODE_SCAN_TIMEOUT`, `SQUELCH_MORE_SENSITIVE`,
`FASTER_CHANNEL_SCAN`, `RSSI_BAR`, `AUDIO_BAR`, `COPY_CHAN_TO_VFO`,
`REDUCE_LOW_MID_TX_POWER`, `BYP_RAW_DEMODULATORS`, `BLMIN_TMP_OFF`,
`SCAN_RANGES`.

**F4HWN pack** (`ENABLE_FEAT_F4HWN_*`): `GAME` (breakout), `SCREENSHOT`,
`SPECTRUM`, `RX_TX_TIMER`, `CHARGING_C`, `SLEEP`, `RESUME_STATE`, `NARROWER`,
`INV`, `CTR`, `SCAN_PROGRESS`, `SCAN_FASTER`, `RESCUE_OPS`, `VOL`, `AUDIO`,
`AUDIO_SCOPE`, `RESET_VFO`, `PMR`, `GMRS_FRS_MURS`, `CA` (Canadian F-lock),
`DEBUG`, `MEM`, `BEAM` (requires AIRCOPY), `QRCODE`, `LOGO`.

**Debug**: `AGC_SHOW_DATA`, `UART_RW_BK_REGS`, `SWD`.

For our fork: turn things **off** aggressively. Each unused module drops both
flash and RAM. Suggested "minimum viable optimized" build:

- KEEP: MESSENGER, SPECTRUM, BIG_FREQ, RSSI_BAR, AUDIO_BAR, WIDE_RX,
  FASTER_CHANNEL_SCAN, AIRCOPY (messenger needs the FSK plumbing),
  SCREENSHOT (debug), SWD, F4HWN base.
- DROP candidates: GAME (breakout), QRCODE, LOGO, REGA, the more obscure
  F4HWN_* alphabet soup, NOAA (US-only), VOICE (heavy, no audio in our case).

---

## 10. Opportunities for our optimized fork

Spotted during this read; in priority order for "perf + UX + sensible
features":

### Performance / size

1. **Enable LTO** — currently `ENABLE_LTO: false`. Free wins.
2. **Investigate `bk4829.c` vs `bk4819.c` duplication** — likely a chunk of
   flash to reclaim.
3. **Font compression** — 46 KB raster. Even simple RLE or only-used-glyphs
   pruning could free a lot of flash.
4. **AM-fix LUT → binary search**, cache last band index.
5. **Dirty-rect rendering** for the LCD — status bar updates 2× per second
   currently redraw the whole screen.
6. **BK4819 register write cache** — skip SPI when the value didn't change.
7. **EEPROM/flash write coalescing** — every settings auto-save burns a
   sector erase on the QSPI; could buffer and batch.
8. **Coalesce 10 ms ISR work** — some decrements only need 500 ms cadence
   (e.g. `gSerialConfigCountDown_500ms` is already there, but others aren't).

### UX

9. **Persistent inbox/outbox** — currently RAM-only, lost on power-cycle.
   Move into the 4 KB messenger flash sector with a small ring buffer.
10. **Message threading** — flat list today; group by `from` peer.
11. **Longer callsigns** (current limit 6 editable / 8 wire). Bump wire field.
12. **Search / archive / unread filter** in inbox.
13. **Calibration menu exposure** for `BK4819_XTAL_FREQ_LOW` (ppm trim) —
    currently hidden behind F-lock boot mode.
14. **CHIRP integration** is on the planned-features list — finish the
    `tools/chirp/` driver and lock down a protocol.

### Sensible features

15. **Resurrect `fw-pack.py`** in the CMake post-build so we can ship signed
    `.packed.bin` updates again. Today only raw `.bin/.hex` come out.
16. **Fix CI artifact path** (`.github/workflows/main.yml:24` references
    `compiled-firmware/f4hwn.packed.bin`, doesn't match Docker output dir).
17. **Range-check / ping-pong** (already on planned list) — leverage the
    existing PING/PONG packet types in `messenger_packet.h`.
18. **Mesh relay** — TTL field is already in the packet and decremented as
    "ttl_remain", but no relay forwarder is wired up yet. The framework is
    there; ~100 LOC to make it useful.
19. **Better PTT timeout UX** — current alert is a beep+blink loop; could be
    a clearer countdown on the LCD's last 10 seconds.

### Code health

20. **Top-level God files** (`app.c` 2321, `menu.c` 2358, `ui/main.c` 2250,
    `spectrum.c` 2613, `settings.c` 1437) — these will hurt anyone trying to
    add a feature. Worth gradual extraction even before functional changes.
21. **The `external/CMSIS_5/` tree** appears unused (CMakeLists doesn't pull
    from it). Probably deletable — frees git LFS quota.

---

## 11. Conventions to follow when editing this codebase

- **Tabs are 4 spaces.** All files I've sampled use 4-space indent.
- **C11**, `-std=gnu11`, `-Wall` etc. — no C++.
- All globals are `g*Camel` (DualTachyon style); messenger module uses
  `s_snake_case` for file-static state.
- Feature gating is **always** `#ifdef ENABLE_FOO` — match this pattern when
  adding optional features; wire the flag into `App/CMakeLists.txt` and
  `CMakePresets.json`.
- New `.c` files go under the matching subdir (`app/`, `driver/`, `ui/`,
  `helper/`) and **must** be listed in `App/CMakeLists.txt:target_sources`,
  either unconditionally or via `enable_feature(... file.c)`.
- BK4819 register access must happen from the foreground (main loop), never
  from the SysTick ISR.
- No `malloc` after init. All buffers are static or stack.
- Don't break the messenger's voice-path snapshot/restore around any new RF
  TX paths — that's load-bearing for the "voice doesn't die after sending a
  message" guarantee.
- Storage: keep messenger data inside the 4 KB sector at `0x012000`; keep
  the legacy 24Cxx-compat region for channel memories.

---

## 12. Git / branch policy (this session)

- Working branch: `claude/uv-k1-firmware-study-fpSNS`.
- All changes commit to that branch; push with `git push -u origin <branch>`.
- Don't open a PR unless explicitly asked.
- The remote `nphil/natesheng-fw` is the only repo I'm allowed to talk to via
  the GitHub MCP tools.

---

## 13. Quick navigation cheat-sheet

| I want to... | Look here |
|--------------|-----------|
| Change the main loop | `App/main.c:310`, `App/app/app.c:925/1382/1571` |
| Add an ISR-timed countdown | `App/scheduler.c:48` |
| Add a menu item | `App/app/menu.c`, `App/ui/menu.c` |
| Add a programmable side-key action | `App/app/action.c`, `enum ACTION_OPT_t` in `App/settings.h:102` |
| Add a new display screen | `App/ui/ui.c` (dispatch), new `App/ui/*.c`, new `App/app/*.c` for state |
| Modify TX/RX behavior | `App/radio.c`, `App/functions.c` |
| Change messenger packet wire format | `App/app/messenger_packet.h/.c` — **bump `MSG_PKT_VERSION`** |
| Change BK4819 modulation | `App/driver/bk4819.c` + `App/driver/bk4819-regs.h` |
| Persist a new user setting | `App/settings.h` (struct field) + `App/settings.c` (load/save) |
| Add a feature flag | `App/CMakeLists.txt` (`enable_feature(...)`) + `CMakePresets.json` |
| Find the LCD framebuffer | `gFrameBuffer[]`, `App/driver/st7565.c` |
| See where messenger RX hooks into the BK4819 ISR | `MSG_RF_OnRadioInterrupt()` in `messenger_rf.c` |

---

_Last updated: study performed by Claude based on GOGUFW 0.3.12 source._
