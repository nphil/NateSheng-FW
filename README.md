# NateSheng-FW

A custom firmware fork for the **Quansheng UV-K5 / K6 / 5R-Plus**
(DP32G030 MCU), targeting **performance, UX, and sensible features**.

**Upstream**: [`armel/uv-k5-firmware-custom`](https://github.com/armel/uv-k5-firmware-custom)
(F4HWN v4.3, on EGZUMER v0.22, on DualTachyon's open base).

---

## Repository layout

```
.                       # ← F4HWN UV-K5 firmware source (build target)
├── Makefile            # make → firmware → firmware.packed.bin
├── firmware.ld         # 60 KB FLASH @ 0x00000000, 16 KB RAM
├── main.c, app/, driver/, ui/, helper/, bsp/, hardware/
├── fw-pack.py          # Wraps .bin into stock-updater-compatible .packed.bin
├── compile-with-docker.sh
│
├── CLAUDE.md           # ★ Working notes — start here for context
├── README.md           # (this file)
│
└── reference/
    └── k1-gogufw/      # GOGUFW 0.3.12 for UV-K1 (PY32F071) — preserved
                        # as the source of the FSK messenger subsystem we'll
                        # back-port. NOT built as part of the K5 firmware.
```

## Build

```bash
./compile-with-docker.sh       # uses Docker, no host toolchain needed
# or with arm-none-eabi-gcc on the host:
make                           # → firmware.bin + firmware.packed.bin
```

`firmware.packed.bin` is what you flash with the **stock Quansheng updater**
(or [`k5prog`](https://github.com/sq5bpf/k5prog)).

**Recommended: back up your EEPROM first** with `k5prog -r` before flashing
any non-stock firmware.

## What's coming

Tracked in `CLAUDE.md §10`:

- Back-port the **FSK text messenger** from the GOGUFW K1 fork
  (`reference/k1-gogufw/App/app/messenger*`) — the marquee feature
- Aggressive feature trim to fit messenger inside the 60 KB flash budget
- Font pruning, dirty-rect LCD updates, BK4819 register write cache
- A working GitHub Actions build CI

## License

Apache 2.0. See `LICENSE`. Original copyright DualTachyon; F4HWN and
EGZUMER contributions per their respective copyright notices in source files.
