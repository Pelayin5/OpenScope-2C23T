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

    // Byte 9 bit 7: extended frame, 8 more bytes after the tail (acquisition and trigger type).
    FPGA_TRIGGER_EXTENDED = 1u << 7,
    FPGA_TRIGGER_EXT_BYTES = 8,
    FPGA_ACQ_NORMAL = 0,
    FPGA_ACQ_PEAK = 1,
    // Extension byte 1: trigger type [1:0] and pulse condition [3:2]; byte 2: window high
    // level; bytes 4..7: X, Y in source samples (MSB first).
    FPGA_TRIGGER_TYPE_EDGE = 0,
    FPGA_TRIGGER_TYPE_PULSE = 1,
    FPGA_TRIGGER_TYPE_TIMEOUT = 2,
    FPGA_TRIGGER_TYPE_WINDOW = 3,
    FPGA_PULSE_LE = 0,     // width <= X
    FPGA_PULSE_GT = 1,     // width > X
    FPGA_PULSE_RANGE = 2,  // X < width <= Y

    // Extended status: read-back bytes 24..39.
    FPGA_EXT_STATUS_BYTES = 16,
    FPGA_EXT_STATUS_VERSION = 0x02,
    FPGA_EXT_STATUS_COUNTER = 1,   // flags, then E (3 bytes) and T (4 bytes), MSB first
    FPGA_EXT_STATUS_FEATURES = 9,
    FPGA_EXT_STATUS_ACQ = 10,
    FPGA_FEATURE_PEAK = 1u << 0,
    FPGA_FEATURE_TRIGGER_TYPES = 1u << 1,
    FPGA_FEATURE_COUNTER = 1u << 2,
    // Frequency counter flags: E periods took T fast clocks (reciprocal, gate ~100 ms).
    FPGA_COUNTER_VALID = 1u << 0,
    FPGA_COUNTER_NO_SIGNAL = 1u << 1,
    FPGA_COUNTER_RUNNING = 1u << 2,
    FPGA_COUNTER_SOURCE_CH2 = 1u << 3,
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
void fpga_set_trigger_ext(const uint8_t *ext);
uint8_t fpga_read_trigger_status(uint8_t status[FPGA_TRIGGER_STATUS_BYTES]);
uint8_t fpga_read_ext_status(uint8_t status[FPGA_TRIGGER_STATUS_BYTES],
                             uint8_t ext_status[FPGA_EXT_STATUS_BYTES]);

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
