#include "../src/hw.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t flash[512];
static uint32_t *test_reg(uint32_t addr) {
    assert(addr >= 0x080FF800u && addr < 0x080FF800u + sizeof(flash));
    return &flash[(addr - 0x080FF800u) / 4u];
}
#undef REG32
#define REG32(addr) (*test_reg(addr))
// Test the real record loader without Cortex-M interrupt instructions.
#define __asm__
#define volatile(...)
#include "../src/settings.c"
#undef volatile
#undef __asm__

static unsigned record_count;

static void put_record(uint8_t type, uint16_t payload) {
    flash[record_count++] = settings_state_record(type, payload);
}

static void put_u32(uint8_t lo_type, uint32_t value) {
    put_record(lo_type, (uint16_t)value);
    put_record((uint8_t)(lo_type + 1u), (uint16_t)(value >> 16));
}

static void test_split_values(void) {
    settings_state_t settings;

    memset(flash, 0xFF, sizeof(flash));
    record_count = 0;
    put_u32(SETTINGS_STATE_SIGGEN_FREQ_LO, 65536u);
    put_u32(SETTINGS_STATE_BODE_START_LO, 131072u);
    put_u32(SETTINGS_STATE_BODE_STOP_LO, 196608u);
    put_u32(SETTINGS_STATE_SIGGEN_SWEEP_START_LO, 65536u);
    put_u32(SETTINGS_STATE_SIGGEN_SWEEP_STOP_LO, 262144u);
    assert(settings_load(&settings));
    assert(settings.siggen_freq_hz == 65536u);
    assert(settings.bode_start_hz == 131072u && settings.bode_stop_hz == 196608u);
    assert(settings.siggen_sweep_start_hz == 65536u && settings.siggen_sweep_stop_hz == 262144u);

    // Out of range values are still clamped once everything is loaded.
    memset(flash, 0xFF, sizeof(flash));
    record_count = 0;
    put_u32(SETTINGS_STATE_SIGGEN_FREQ_LO, 0u);
    put_u32(SETTINGS_STATE_SIGGEN_SWEEP_MS_LO, 0x00010000u);
    put_u32(SETTINGS_STATE_SIGGEN_FM_FREQ_LO, 0x00010000u);
    put_record(SETTINGS_STATE_SCOPE_TRIGGER, 0u);
    assert(settings_load(&settings));
    assert(settings.siggen_freq_hz == SETTINGS_SIGGEN_DEFAULT_FREQ_HZ);
    assert(settings.siggen_sweep_ms == 9999u && settings.siggen_fm_freq_hz == SETTINGS_SIGGEN_FM_MAX_HZ);
    assert(settings.scope_trigger_level == 12u);
}

// Trigger type, pulse width condition, T1/T2, window high level and acquisition (open HW4
// bitstream) in two records after the generator ones.
static void test_trigger_records(void) {
    settings_state_t settings;
    uint8_t legacy_mode;

    memset(flash, 0xFF, sizeof(flash));
    record_count = 0;
    assert(!settings_load(&settings));
    assert(settings.scope_trigger_type == 0u && settings.scope_pulse_cond == SETTINGS_SCOPE_PULSE_DEFAULT);
    assert(settings.scope_trigger_time[0] == SETTINGS_SCOPE_TRIGGER_TIME_DEFAULT_1 &&
           settings.scope_trigger_time[1] == SETTINGS_SCOPE_TRIGGER_TIME_DEFAULT_2);
    assert(settings.scope_window_high == SETTINGS_SCOPE_WINDOW_HIGH_DEFAULT && settings.scope_acquisition == 0u);

    put_record(SETTINGS_STATE_SCOPE_TRIG_TYPE, (uint16_t)(3u | (2u << 2) | (1u << 4) | (200u << 8)));
    put_record(SETTINGS_STATE_SCOPE_TRIG_TIME, (uint16_t)(7u | (22u << 5)));
    assert(settings_load(&settings));
    assert(settings.scope_trigger_type == 3u && settings.scope_pulse_cond == 2u && settings.scope_acquisition == 1u);
    assert(settings.scope_window_high == 200u);
    assert(settings.scope_trigger_time[0] == 7u && settings.scope_trigger_time[1] == 22u);

    // out of range values fall back to the defaults
    put_record(SETTINGS_STATE_SCOPE_TRIG_TYPE, (uint16_t)(3u << 2 | 3u << 4 | 5u << 8));
    put_record(SETTINGS_STATE_SCOPE_TRIG_TIME, (uint16_t)(30u | (23u << 5)));
    assert(settings_load(&settings));
    assert(settings.scope_pulse_cond == SETTINGS_SCOPE_PULSE_DEFAULT && settings.scope_acquisition == 0u);
    assert(settings.scope_window_high == 12u);
    assert(settings.scope_trigger_time[0] == SETTINGS_SCOPE_TRIGGER_TIME_DEFAULT_1 &&
           settings.scope_trigger_time[1] == SETTINGS_SCOPE_TRIGGER_TIME_DEFAULT_2);

    // the new state type shares its top byte with the legacy DMM record: neither decodes as the other
    for (uint32_t payload = 0; payload <= 0xFFFFu; ++payload) {
        assert(!settings_legacy_record_valid(settings_state_record(SETTINGS_STATE_SCOPE_TRIG_TYPE, (uint16_t)payload),
                                             &legacy_mode));
    }
    for (uint32_t value = 0; value <= 0xFFu; ++value) {
        uint32_t legacy = SETTINGS_LEGACY_MAGIC | (value << 8) | (uint8_t)(value ^ 0x5Au);
        assert(!settings_state_record_valid(legacy, &settings));
    }
    assert(SETTINGS_STATE_WORDS == 26u);
}

int main(void) {
    test_split_values();
    test_trigger_records();
    printf("settings: split 32-bit records restore without partial clamping, trigger records passed\n");
    return 0;
}
