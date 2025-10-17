// main.c — Pico 2 W (RP2350) PIO CSYNC generator — mirrors RP1 rp1_dpi_pio.c (progressive)
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"

// ---------------- GPIOs (change to suit your wiring) ----------------
#ifndef PIN_HSYNC
#define PIN_HSYNC 2   // HSYNC input
#endif
#ifndef PIN_VSYNC
#define PIN_VSYNC 3   // VSYNC input
#endif
#ifndef PIN_CSYNC
#define PIN_CSYNC 4   // CSYNC output
#endif
// --------------------------------------------------------------------

// ---------------- Display timing (your dtparams) ---------------------
// Pixel clock = 8.056 MHz, H: active=400, fp=10, sync=40, bp=62
static const uint32_t PIXEL_CLK_HZ = 8056000u; // dpi pixel clock (Hz)
static const uint16_t H_TOTAL      = 512;      // 400+10+40+62
static const uint16_t H_SYNC_START = 410;      // 400+10
static const uint16_t H_SYNC_END   = 450;      // +40
// --------------------------------------------------------------------

// ------------ Polarity flags (mirror RP1’s DRM flags) ----------------
// Set these to match your source (your captures looked active-low HS & VS).
#define NHSYNC  1   // “Negative HSYNC” (HSYNC active-low)
#define NVSYNC  1   // “Negative VSYNC” (VSYNC active-low)
#define PCSYNC  0   // Invert CSYNC polarity (XOR sideset bit on all instrs)
// --------------------------------------------------------------------

typedef struct {
    uint wrap_target;
    uint wrap;
    int  offset;
    uint sm;
} pio_prog_handle_t;

// Patch the pin field (low 5 bits) of a wait gpio instruction
static inline void patch_wait_gpio_pin(uint16_t *instr, uint gpio_pin) {
    *instr = (uint16_t)((*instr & ~0x001F) | (gpio_pin & 0x1F));
}

static void build_rp1_csync_prog(uint16_t instr[9]) {
    // Exact instruction stream used by the RP1 driver (progressive)
    instr[0] = 0x90A0; // 0: pull   block      side 1
    instr[1] = 0x7040; // 1: out    y,32       side 1
    // .wrap_target = 2
    instr[2] = 0xB322; // 2: mov    x,y        side 1 [3]
    instr[3] = 0x3083; // 3: wait   1 gpio,HS  side 1   (pin patched below)
    instr[4] = 0xA422; // 4: mov    x,y        side 0 [4]
    instr[5] = 0x2003; // 5: wait   0 gpio,HS  side 0   (pin patched below)
    instr[6] = 0x00C7; // 6: jmp    pin,7      side 0   (VS gate; changed if NVSYNC)
    // .wrap = 6 (PVS) or 7 (NVS)
    instr[7] = 0x0047; // 7: jmp    x--,7      side 0   (extend loop)
    instr[8] = 0x1002; // 8: jmp    2          side 1
}

static bool load_rp1_csync(PIO pio, pio_prog_handle_t *h) {
    uint16_t instr[9];
    build_rp1_csync_prog(instr);

    // Patch HS pin into the two wait instructions (#3 and #5)
    patch_wait_gpio_pin(&instr[3], PIN_HSYNC);
    patch_wait_gpio_pin(&instr[5], PIN_HSYNC);

    // Apply polarity like the RP1 driver (DRM_MODE_FLAG_* equivalents)
    if (NVSYNC) {
        // VSYNC active-low: VS=1 (idle) -> mirror (jmp pin,2), VS=0 -> extend
        instr[6] = 0x00C2; // jmp pin,2 side 0
    } // else leave as 0x00C7 (VS active-high → VS=1 -> extend)

    if (NHSYNC) {
        // Flip sense of the two HS waits: toggle bit7 (wait 1 <-> wait 0)
        instr[3] ^= 0x0080;
        instr[5] ^= 0x0080;
    }

    if (PCSYNC) { // keep name parity with RP1; 'positive csync' flag means invert sideset
        for (int i = 0; i < 9; ++i) instr[i] ^= 0x1000; // flip sideset bit12 on all instructions
    }

    struct pio_program prog = { .instructions = instr, .length = 9, .origin = -1 };

    h->sm = pio_claim_unused_sm(pio, true);
    if ((int)h->sm < 0) return false;

    h->offset = pio_add_program(pio, &prog);
    if (h->offset < 0) return false;

    // Wrap target is always #2. Wrap matches RP1 logic:
    //   if NVSYNC => wrap at 7, else wrap at 6.
    h->wrap_target = h->offset + 2;
    h->wrap        = h->offset + (NVSYNC ? 7u : 6u);

    // Configure SM
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, h->wrap_target, h->wrap);

    // 1-bit sideset on CSYNC pin
    sm_config_set_sideset(&c, 1, false, false);
    sm_config_set_sideset_pins(&c, PIN_CSYNC);
    pio_gpio_init(pio, PIN_CSYNC);
    pio_sm_set_consecutive_pindirs(pio, h->sm, PIN_CSYNC, 1, true);

    // VSYNC drives the jmp pin
    sm_config_set_jmp_pin(&c, PIN_VSYNC);

    // Inputs: leave pulls disabled to avoid biasing your source
    gpio_init(PIN_HSYNC); gpio_disable_pulls(PIN_HSYNC); gpio_set_dir(PIN_HSYNC, GPIO_IN);
    gpio_init(PIN_VSYNC); gpio_disable_pulls(PIN_VSYNC); gpio_set_dir(PIN_VSYNC, GPIO_IN);

    pio_sm_init(pio, h->sm, h->offset, &c);
    return true;
}

int main() {
    stdio_init_all();
    sleep_ms(250);
    printf("\n[Pico2W] RP1-style PIO CSYNC (progressive)\n");
    printf("Pins HS=%u VS=%u CS=%u | N(H,V,CS)=(%u,%u,%u)\n",
           PIN_HSYNC, PIN_VSYNC, PIN_CSYNC, NHSYNC, NVSYNC, PCSYNC);

    // Load program
    PIO pio = pio0;
    pio_prog_handle_t h;
    if (!load_rp1_csync(pio, &h)) {
        printf("ERROR: PIO load failed\n");
        while (1) tight_loop_contents();
    }

    // Compute RP1 time constant:
    // tc = ((htotal - 2*hsw) * clk_sys) / pixel_clk
    // Then push (tc - 2)
    uint32_t sys_hz   = clock_get_hz(clk_sys);
    uint16_t hsw      = (uint16_t)(H_SYNC_END - H_SYNC_START);
    uint32_t lhs      = (uint32_t)(H_TOTAL - 2u * hsw);
    uint32_t tc       = (uint32_t)((uint64_t)lhs * (uint64_t)sys_hz / (uint64_t)PIXEL_CLK_HZ);
    uint32_t push_val = (tc >= 2) ? (tc - 2) : 0;

    printf("clk_sys=%u, pixel=%u | htotal=%u, hsw=%u, lhs=%u | tc=%u, push=%u\n",
           sys_hz, PIXEL_CLK_HZ, H_TOTAL, hsw, lhs, tc, push_val);

    // Place time constant into FIFO; start the SM
    pio_sm_put_blocking(pio, h.sm, push_val);
    pio_sm_set_enabled(pio, h.sm, true);

    // Heartbeat/debug
    while (1) {
        uint pc = pio_sm_get_pc(pio, h.sm);
        int idx = (int)pc - h.offset;
        printf("SM%u pc=%u idx=%d | HS=%d VS=%d\n",
               h.sm, pc, idx, gpio_get(PIN_HSYNC), gpio_get(PIN_VSYNC));
        sleep_ms(250);
    }
}
