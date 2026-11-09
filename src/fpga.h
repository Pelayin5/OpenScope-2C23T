#pragma once

#include <stdint.h>

#ifndef FPGA_OPEN_BITSTREAM
#define FPGA_OPEN_BITSTREAM 0
#endif

// Fast FPGA clock: DDS step and capture entry rate at span 0x100000. Stock HW4 runs its PLL at
// 100 MHz; the open bitstream has no PLL and runs from the 50 MHz oscillator.
#if FPGA_OPEN_BITSTREAM
#define FPGA_FAST_CLOCK_HZ 50000000u
#else
#define FPGA_FAST_CLOCK_HZ 100000000u
#endif

enum {
    FPGA_SAMPLE_COUNT = 2048,
    FPGA_SCOPE_BUFFER_BYTES = FPGA_SAMPLE_COUNT * 2,
};

// Open HW4 bitstream trigger: the 8 tail bytes of the timing frame.
// Stock tail bytes 00..07 keep the free-running capture.
enum {
    FPGA_TRIGGER_TAIL_BYTES = 8,
    FPGA_TRIGGER_MAGIC = 0x54,
    FPGA_TRIGGER_MODE_AUTO = 0,
    FPGA_TRIGGER_MODE_NORMAL = 1,
    FPGA_TRIGGER_MODE_SINGLE = 2,
    FPGA_TRIGGER_MODE_FORCE = 3,
    FPGA_TRIGGER_SOURCE_CH2 = 1u << 2,
    FPGA_TRIGGER_EDGE_FALLING = 1u << 3,
    FPGA_TRIGGER_PRE_MAX = 2047,

    FPGA_TRIGGER_STATUS_BYTES = 8,
    FPGA_TRIGGER_STATUS_ID = 0x54,
    FPGA_TRIGGER_STATUS_VERSION = 0x01,
    FPGA_TRIGGER_STATUS_READY = 1u << 0,
    FPGA_TRIGGER_STATUS_TRIGGERED = 1u << 1,
    FPGA_TRIGGER_STATUS_FORCED = 1u << 2,
    FPGA_TRIGGER_STATUS_ENABLED = 1u << 3,
    FPGA_TRIGGER_STATUS_WAITING = 1u << 4,
    FPGA_TRIGGER_STATUS_HELD = 1u << 5,
};

void fpga_init_once(void);
uint8_t fpga_ready(void);
void fpga_write_timing(uint32_t tuning_word, uint32_t span);
void fpga_write_generator_timing(uint32_t tuning_word);
void fpga_write_scope_timing(uint32_t span);
void fpga_write_signal_buffer(const uint8_t *data, uint16_t len);
void fpga_capture_latch(void);
uint8_t fpga_capture_ready(void);
void fpga_capture_ready_irq_handler(void);
uint8_t fpga_capture_read(uint8_t *dst, uint16_t len);
uint8_t fpga_capture_read_slow_point(uint8_t sample[2]);
void fpga_set_trigger_tail(const uint8_t *tail);
uint8_t fpga_read_trigger_status(uint8_t status[FPGA_TRIGGER_STATUS_BYTES]);

// Open HW4 bitstream: bus self-test through the frame read-back (all 8 data lines, both ways).
enum {
    FPGA_BUS_TEST_BYTES = 16,
};

typedef struct {
    uint8_t done;
    uint8_t bad_bits;
    uint8_t sent[FPGA_BUS_TEST_BYTES];
    uint8_t read[FPGA_BUS_TEST_BYTES];
} fpga_bus_test_t;

void fpga_bus_test_run(void);
const fpga_bus_test_t *fpga_bus_test_result(void);
