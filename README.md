# pico2w_csync_step1 — Progressive CSync on Raspberry Pi Pico 2 W (Step 1)

This is a **step‑1** port of your composite‑sync PIO logic to a **standalone Pico SDK** application:
- No Linux / DRM / RP1‑DPI dependencies
- Inputs: HSYNC, VSYNC on user‑selectable GPIOs
- Output: CSYNC on a user‑selectable GPIO
- Implements the progressive‐scan variant (interlaced/TV style will be Step 2)

## Pin defaults (edit in `main.c`)
```
PIN_HSYNC = 2
PIN_VSYNC = 3
PIN_CSYNC = 4
```
Change `INVERT_*` flags if your incoming HSYNC/VSYNC are active‑low.

## Timing
Set `PIXEL_CLK_HZ`, `H_TOTAL`, `H_SYNC_START`, `H_SYNC_END`. The program computes the extend constant `tc` to match the original logic.

## Build
```
export PICO_SDK_PATH=/path/to/pico-sdk
mkdir build && cd build
cmake ..
make -j
```
Flash the resulting `pico2w_csync_step1.uf2` to your Pico 2/W.

## Notes
- Output is 3.3V TTL. For SCART/75Ω you need proper resistor network/AC coupling.
- This step does **progressive only**. Next steps will add interlaced/TV‑style paths and runtime configuration.
