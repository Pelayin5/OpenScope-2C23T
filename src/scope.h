#pragma once

#include "fpga.h"

#include <stdint.h>

#ifndef FPGA_HW_TRIGGER
#define FPGA_HW_TRIGGER 0
#endif
#if FPGA_HW_TRIGGER && !HW_TARGET_HW40
#error "FPGA_HW_TRIGGER needs the open HW4 bitstream (HW_TARGET_HW40=1)"
#endif

enum {
    SCOPE_HW_SPAN_MAX = 0x100000,
};

enum {
    SCOPE_HW_TRIGGER_AUTO,
    SCOPE_HW_TRIGGER_NORMAL,
    SCOPE_HW_TRIGGER_SINGLE,
};

enum {
    SCOPE_HW_ACQ_NORMAL,
    SCOPE_HW_ACQ_PEAK,
};

// Hardware trigger and acquisition settings (open HW4 bitstream).
typedef struct {
    uint8_t enabled;
    uint8_t mode;
    uint8_t source;
    uint8_t falling;
    uint8_t level;
    uint16_t pre_samples;
    // Peak detect: pair 2k holds the minimum and pair 2k+1 the maximum of the same group.
    uint8_t acquisition;
} scope_hw_trigger_t;

// At SCOPE_HW_SPAN_MAX the FPGA stores one byte per fast clock, alternating CH1 and CH2, so a
// sample pair takes two fast clocks (20 ns stock, 40 ns with the open 50 MHz bitstream).
#define SCOPE_HW_PAIR_NS (2000000000u / FPGA_FAST_CLOCK_HZ)

// The FPGA takes one sample every SCOPE_HW_PAIR_NS * SCOPE_HW_SPAN_MAX / span. The span keeps
// the stock sample period (unit_ns * 2 / 5) wherever the fast clock allows it.
static inline uint32_t scope_hw_span_for_unit_ns(uint32_t unit_ns) {
    uint32_t span = (SCOPE_HW_SPAN_MAX * 5u / 2u * SCOPE_HW_PAIR_NS) / unit_ns;

    return span > SCOPE_HW_SPAN_MAX ? SCOPE_HW_SPAN_MAX : span;
}

static inline uint32_t scope_hw_sample_period_ns(uint32_t unit_ns) {
    uint32_t span = scope_hw_span_for_unit_ns(unit_ns);

    return (SCOPE_HW_PAIR_NS * SCOPE_HW_SPAN_MAX + span / 2u) / span;
}

// num / den without a 64-bit divide helper (the firmware links no runtime library).
static inline uint64_t scope_hw_div_u64(uint64_t num, uint32_t den) {
    uint64_t rem = 0;
    uint64_t quot = 0;

    if (!den) {
        return 0;
    }
    for (int8_t bit = 63; bit >= 0; --bit) {
        rem = (rem << 1) | ((num >> bit) & 1u);
        quot <<= 1;
        if (rem >= den) {
            rem -= den;
            quot |= 1u;
        }
    }
    return quot;
}

void scope_hw_configure(uint8_t timebase, uint8_t vdiv);
void scope_hw_configure_channels(uint8_t timebase,
                                 uint8_t ch1_vdiv,
                                 uint8_t ch2_vdiv,
                                 uint8_t ch1_dc,
                                 uint8_t ch2_dc,
                                 uint16_t ch1_dac,
                                 uint16_t ch2_dac);
void scope_hw_set_offsets(uint16_t ch1_dac, uint16_t ch2_dac);
void scope_hw_arm(void);
uint8_t scope_hw_capture(uint8_t *dst, uint16_t len, uint8_t timebase);
uint8_t scope_hw_capture_point(uint8_t sample[2], uint8_t timebase);
void scope_hw_slow_start(uint8_t timebase);
void scope_hw_slow_stop(void);
uint8_t scope_hw_slow_snapshot(uint8_t *ch1,
                               uint8_t *ch2,
                               uint16_t max_points,
                               uint16_t *count,
                               uint16_t *seq);
void scope_hw_slow_irq_handler(void);
uint8_t scope_hw_ready(void);
uint8_t scope_hw_enabled(void);
uint16_t scope_hw_frame_count(void);
uint16_t scope_hw_wait_count(void);
uint16_t scope_hw_error_count(void);
uint8_t scope_hw_last_status(void);
void scope_hw_set_trigger(const scope_hw_trigger_t *trigger);
uint8_t scope_hw_capture_acquisition(void);
uint8_t scope_hw_counter_mhz(uint8_t channel, uint64_t *mhz);
uint8_t scope_hw_trigger_position(uint16_t *pre_samples);
uint8_t scope_hw_trigger_status(uint8_t status[FPGA_TRIGGER_STATUS_BYTES]);
