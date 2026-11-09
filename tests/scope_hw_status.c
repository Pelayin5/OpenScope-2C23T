// Host regression test: scope.c with the open HW4 bitstream (FPGA_HW_TRIGGER=1) against a
// mocked FPGA register read-back: extended frame bytes, peak detect flag, counter decode.
#include <stdint.h>

// The interrupt masking (cpsid/cpsie) of the roll-mode code is ARM-only; it is not run here.
#define __asm__
#define volatile(...)
#include "../src/scope.c"
#undef volatile
#undef __asm__

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t tail[FPGA_TRIGGER_TAIL_BYTES];
static uint8_t ext[FPGA_TRIGGER_EXT_BYTES];
static uint8_t status[FPGA_TRIGGER_STATUS_BYTES];
static uint8_t ext_status[FPGA_EXT_STATUS_BYTES];
static uint8_t status_valid;
static unsigned latches;

void delay_ms(uint32_t ms) { (void)ms; }
void fpga_init_once(void) {}
uint8_t fpga_ready(void) { return 1; }
void fpga_write_scope_timing(uint32_t span) { (void)span; }
void fpga_capture_latch(void) { ++latches; }
uint8_t fpga_capture_ready(void) { return 1; }
uint8_t fpga_capture_read(uint8_t *dst, uint16_t len) {
    memset(dst, 128, len);
    return 1;
}
void fpga_set_trigger_tail(const uint8_t *t) {
    for (unsigned i = 0; i < FPGA_TRIGGER_TAIL_BYTES; ++i) tail[i] = t ? t[i] : (uint8_t)i;
}
void fpga_set_trigger_ext(const uint8_t *e) {
    for (unsigned i = 0; i < FPGA_TRIGGER_EXT_BYTES; ++i) ext[i] = e ? e[i] : 0u;
}
uint8_t fpga_read_ext_status(uint8_t st[FPGA_TRIGGER_STATUS_BYTES],
                             uint8_t xs[FPGA_EXT_STATUS_BYTES]) {
    memcpy(st, status, sizeof(status));
    memcpy(xs, ext_status, sizeof(ext_status));
    return status_valid;
}

static uint8_t buffer[FPGA_SCOPE_BUFFER_BYTES];

static void capture(void) {
    unsigned before = latches;
    assert(scope_hw_capture(buffer, sizeof(buffer), 11u));
    assert(latches == before + 1u);   // re-armed after the read
}

static void set_counter(uint8_t flags, uint32_t periods, uint32_t clocks) {
    uint8_t *c = &ext_status[FPGA_EXT_STATUS_COUNTER];
    c[0] = flags;
    c[1] = (uint8_t)(periods >> 16); c[2] = (uint8_t)(periods >> 8); c[3] = (uint8_t)periods;
    c[4] = (uint8_t)(clocks >> 24); c[5] = (uint8_t)(clocks >> 16);
    c[6] = (uint8_t)(clocks >> 8); c[7] = (uint8_t)clocks;
}

static void test_config(void) {
    scope_hw_trigger_t t = {1, SCOPE_HW_TRIGGER_NORMAL, 0, 0, 140u, 600u, SCOPE_HW_ACQ_NORMAL,
                            SCOPE_HW_TYPE_EDGE, 0, 0, 0, 0};

    scope_hw_set_trigger(&t);
    assert(tail[0] == FPGA_TRIGGER_MAGIC && tail[1] == FPGA_TRIGGER_MODE_NORMAL && !ext[0]);
    // Peak detect: extended frame, acquisition byte.
    t.acquisition = SCOPE_HW_ACQ_PEAK;
    scope_hw_set_trigger(&t);
    assert(tail[1] == (FPGA_TRIGGER_MODE_NORMAL | FPGA_TRIGGER_EXTENDED) && ext[0] == FPGA_ACQ_PEAK);
    // Peak detect without the hardware trigger: forced captures.
    t.enabled = 0;
    scope_hw_set_trigger(&t);
    assert(tail[0] == FPGA_TRIGGER_MAGIC &&
           tail[1] == (FPGA_TRIGGER_MODE_FORCE | FPGA_TRIGGER_EXTENDED) && ext[0] == FPGA_ACQ_PEAK);
    // Neither: the stock bytes.
    t.acquisition = SCOPE_HW_ACQ_NORMAL;
    scope_hw_set_trigger(&t);
    for (unsigned i = 0; i < FPGA_TRIGGER_TAIL_BYTES; ++i) assert(tail[i] == i);
    assert(!ext[0]);

    // Pulse width X < w <= Y: type, condition, window level and widths in the extension.
    t.enabled = 1;
    t.type = SCOPE_HW_TYPE_PULSE;
    t.pulse = SCOPE_HW_PULSE_RANGE;
    t.width_lo = 0x0123u;
    t.width_hi = 0x4567u;
    t.level_high = 200u;
    scope_hw_set_trigger(&t);
    assert(tail[1] == (FPGA_TRIGGER_MODE_NORMAL | FPGA_TRIGGER_EXTENDED) && ext[0] == 0 &&
           ext[1] == (FPGA_TRIGGER_TYPE_PULSE | (FPGA_PULSE_RANGE << 2)) && ext[2] == 200u &&
           ext[4] == 0x01u && ext[5] == 0x23u && ext[6] == 0x45u && ext[7] == 0x67u);
    t.type = SCOPE_HW_TYPE_WINDOW;
    scope_hw_set_trigger(&t);
    assert(ext[1] == (FPGA_TRIGGER_TYPE_WINDOW | (FPGA_PULSE_RANGE << 2)));
    // A type without the hardware trigger means nothing: stock bytes again.
    t.enabled = 0;
    scope_hw_set_trigger(&t);
    for (unsigned i = 0; i < FPGA_TRIGGER_TAIL_BYTES; ++i) assert(tail[i] == i);
    for (unsigned i = 0; i < FPGA_TRIGGER_EXT_BYTES; ++i) assert(!ext[i]);
    t.type = SCOPE_HW_TYPE_EDGE;
}

static void test_status(void) {
    uint64_t mhz = 0;
    uint16_t pre = 0;
    scope_hw_trigger_t t = {1, SCOPE_HW_TRIGGER_NORMAL, 0, 0, 140u, 600u, SCOPE_HW_ACQ_PEAK,
                            SCOPE_HW_TYPE_EDGE, 0, 0, 0, 0};

    scope_hw_set_trigger(&t);
    status[0] = FPGA_TRIGGER_STATUS_ID;
    status[6] = FPGA_TRIGGER_STATUS_VERSION;
    status[1] = FPGA_TRIGGER_STATUS_READY | FPGA_TRIGGER_STATUS_TRIGGERED | FPGA_TRIGGER_STATUS_ENABLED;
    // rot 0x054, trigger at the start of the group at entry 0x504: pair (0x504 - 0x54) / 2
    status[2] = 0x00; status[3] = 0x54; status[4] = 0x05; status[5] = 0x04;
    status[7] = tail[1];
    ext_status[0] = FPGA_EXT_STATUS_VERSION;
    ext_status[FPGA_EXT_STATUS_FEATURES] = FPGA_FEATURE_PEAK;
    ext_status[FPGA_EXT_STATUS_ACQ] = FPGA_ACQ_PEAK;
    status_valid = 3;
    capture();
    assert(scope_hw_capture_acquisition() == SCOPE_HW_ACQ_PEAK);
    assert(scope_hw_trigger_position(&pre) && pre == 600u);
    // No counter feature: no hardware frequency.
    set_counter(FPGA_COUNTER_VALID | FPGA_COUNTER_RUNNING, 55u, 66000u);
    capture();
    assert(!scope_hw_counter_mhz(0, &mhz));
    // Peak refused by the FPGA (full rate): normal buffer.
    ext_status[FPGA_EXT_STATUS_ACQ] = FPGA_ACQ_NORMAL;
    capture();
    assert(scope_hw_capture_acquisition() == SCOPE_HW_ACQ_NORMAL);
    // Bitstream without the extended status: normal buffer, no counter.
    ext_status[FPGA_EXT_STATUS_ACQ] = FPGA_ACQ_PEAK;
    status_valid = 1;
    capture();
    assert(scope_hw_capture_acquisition() == SCOPE_HW_ACQ_NORMAL && !scope_hw_counter_mhz(0, &mhz));
    status_valid = 3;
}

static void test_counter(void) {
    uint64_t mhz = 0;

    ext_status[FPGA_EXT_STATUS_FEATURES] = FPGA_FEATURE_PEAK | FPGA_FEATURE_COUNTER;
    // 55 periods in 66000 clocks of 50 MHz: 41666.666 Hz
    set_counter(FPGA_COUNTER_VALID | FPGA_COUNTER_RUNNING, 55u, 66000u);
    capture();
    assert(scope_hw_counter_mhz(0, &mhz) && mhz == 41666666u);
    assert(!scope_hw_counter_mhz(1, &mhz));
    // 6.943 Hz: one period of 7201383 clocks
    set_counter(FPGA_COUNTER_VALID | FPGA_COUNTER_RUNNING | FPGA_COUNTER_SOURCE_CH2, 1u, 7201383u);
    capture();
    assert(!scope_hw_counter_mhz(0, &mhz));
    assert(scope_hw_counter_mhz(1, &mhz) && mhz == 6943u);
    // 12.5 MHz (the fastest edge rate): the quotient exceeds 32 bits
    set_counter(FPGA_COUNTER_VALID | FPGA_COUNTER_RUNNING, 1250000u, 5000000u);
    capture();
    assert(scope_hw_counter_mhz(0, &mhz) && mhz == 12500000000ull);
    // no signal / running without a result / stopped: software fallback
    set_counter(FPGA_COUNTER_NO_SIGNAL | FPGA_COUNTER_RUNNING, 1u, 7201383u);
    capture();
    assert(!scope_hw_counter_mhz(0, &mhz));
    set_counter(FPGA_COUNTER_RUNNING, 0u, 0u);
    capture();
    assert(!scope_hw_counter_mhz(0, &mhz));
    assert(scope_hw_div_u64(0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFu) == 0x100000001ull);
    assert(scope_hw_div_u64(1000u, 0u) == 0u);
}

int main(void) {
    test_config();
    test_status();
    test_counter();
    puts("scope hardware status decode tests passed (peak detect, counter)");
    return 0;
}
