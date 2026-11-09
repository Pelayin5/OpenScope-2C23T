// Host regression test: UI trigger settings -> FPGA hardware trigger (FPGA_HW_TRIGGER=1).
#include "../src/ui.c"
#include "../src/fpga.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t hw_enabled;
static uint8_t hw_mode;
static uint8_t hw_source;
static uint8_t hw_falling;
static uint8_t hw_level;
static uint16_t hw_pre;
static uint8_t hw_hit;
static uint16_t hw_hit_pre;

static uint8_t hw_acquisition;
static uint8_t hw_capture_acq;
static scope_hw_trigger_t hw_trigger;

void scope_hw_set_trigger(const scope_hw_trigger_t *trigger) {
    hw_enabled = trigger->enabled;
    hw_mode = trigger->mode;
    hw_source = trigger->source;
    hw_falling = trigger->falling;
    hw_level = trigger->level;
    hw_pre = trigger->pre_samples;
    hw_acquisition = trigger->acquisition;
    hw_trigger = *trigger;
}

uint8_t scope_hw_capture_acquisition(void) {
    return hw_capture_acq;
}

static uint8_t hw_counter_ch = 0xFFu;
static uint64_t hw_counter_value;

uint8_t scope_hw_counter_mhz(uint8_t channel, uint64_t *mhz) {
    if (channel != hw_counter_ch) {
        return 0;
    }
    *mhz = hw_counter_value;
    return 1;
}

uint8_t scope_hw_trigger_position(uint16_t *pre_samples) {
    if (!hw_hit) {
        return 0;
    }
    *pre_samples = hw_hit_pre;
    return 1;
}

// scope_adjust_selected_param reaches the V/DIV path, which re-arms the capture.
void scope_hw_arm(void) {
}

static void setup(void) {
    ui.scope_display = SCOPE_DISPLAY_YT;
    ui.scope_timebase = 11u;
    ui.scope_trigger_source = 1u;
    ui.scope_trigger_edge = 0;
    ui.scope_trigger_level = 140u;
    ui.scope_trigger_mode = SCOPE_TRIGGER_NORMAL;
    scope_h_pos = 0;
    scope_trigger_locked = 0;
    scope_trigger_offset = 0;
    scope_auto_zero_active = 0;
    scope_ch_enabled[0] = 1;
    scope_ch_enabled[1] = 1;
    hw_hit = 0;
}

static void test_settings(void) {
    setup();
    scope_hw_trigger_sync();
    assert(hw_enabled && hw_mode == SCOPE_HW_TRIGGER_NORMAL && hw_source == 0u &&
           !hw_falling && hw_level == 140u);
    ui.scope_trigger_source = 2u;
    ui.scope_trigger_edge = 1u;
    ui.scope_trigger_mode = SCOPE_TRIGGER_SINGLE;
    scope_hw_trigger_sync();
    assert(hw_enabled && hw_mode == SCOPE_HW_TRIGGER_SINGLE && hw_source == 1u && hw_falling);
    ui.scope_trigger_mode = SCOPE_TRIGGER_AUTO;
    scope_hw_trigger_sync();
    assert(hw_mode == SCOPE_HW_TRIGGER_AUTO);

    // Software-only cases keep the stock free-running capture.
    scope_ch_enabled[1] = 0;
    scope_hw_trigger_sync();
    assert(!hw_enabled);
    setup();
    scope_auto_zero_active = 1;
    scope_hw_trigger_sync();
    assert(!hw_enabled);
    setup();
    ui.scope_display = SCOPE_DISPLAY_ROLL;
    scope_hw_trigger_sync();
    assert(!hw_enabled);
    setup();
    ui.scope_timebase = SCOPE_SLOW_ROLL_TIMEBASE_START;
    scope_hw_trigger_sync();
    assert(!hw_enabled);
}

static void test_window(void) {
    setup();
    for (uint8_t timebase = 0; timebase < SCOPE_SLOW_ROLL_TIMEBASE_START; ++timebase) {
        ui.scope_timebase = timebase;
        uint16_t visible = scope_visible_sample_count();
        for (int16_t pos = -SCOPE_H_POS_LIMIT; pos <= SCOPE_H_POS_LIMIT; ++pos) {
            uint16_t offset = 0;
            scope_h_pos = pos;
            scope_hw_trigger_sync();
            assert(hw_enabled && hw_pre <= FPGA_TRIGGER_PRE_MAX);
            hw_hit = 1;
            hw_hit_pre = hw_pre;
            assert(scope_hw_trigger_offset(&offset));
            // The trigger sample is drawn at the same screen position as a software trigger.
            assert(offset + scope_trigger_screen_sample_pos() == hw_pre);
            assert(offset + visible <= SCOPE_SAMPLE_COUNT);
            scope_trigger_locked = 1;
            scope_trigger_offset = offset;
            assert(scope_capture_window_start() == offset);
            scope_trigger_locked = 0;
            // A stale position outside the window falls back to the software search.
            hw_hit_pre = (uint16_t)(scope_trigger_screen_sample_pos() + SCOPE_SAMPLE_COUNT - visible + 1u);
            assert(!scope_hw_trigger_offset(&offset));
            hw_hit = 0;
            assert(!scope_hw_trigger_offset(&offset));
        }
    }
}

static void test_peak(void) {
    setup();
    scope_acquisition = SCOPE_HW_ACQ_PEAK;
    scope_hw_trigger_sync();
    assert(hw_enabled && hw_acquisition == SCOPE_HW_ACQ_PEAK);
    // The undecimated span already keeps every sample (up to 500NS at 100 MHz, 1US at 50 MHz).
    ui.scope_timebase = FPGA_FAST_CLOCK_HZ > 50000000u ? 3u : 4u;
    assert(scope_hw_span_for_unit_ns(scope_timebase_unit_ns_value()) == SCOPE_HW_SPAN_MAX);
    scope_hw_trigger_sync();
    assert(hw_acquisition == SCOPE_HW_ACQ_NORMAL);
    ui.scope_timebase++;
    assert(scope_hw_span_for_unit_ns(scope_timebase_unit_ns_value()) < SCOPE_HW_SPAN_MAX);
    scope_hw_trigger_sync();
    assert(hw_acquisition == SCOPE_HW_ACQ_PEAK);
    // Per-sample views fall back to normal acquisition.
    scope_fft_src = 1u;
    scope_hw_trigger_sync();
    assert(hw_acquisition == SCOPE_HW_ACQ_NORMAL);
    scope_fft_src = 0;
    scope_math_mode = 1u;
    scope_hw_trigger_sync();
    assert(hw_acquisition == SCOPE_HW_ACQ_NORMAL);
    scope_math_mode = 0;
    ui.scope_display = SCOPE_DISPLAY_ROLL;
    scope_hw_trigger_sync();
    assert(hw_acquisition == SCOPE_HW_ACQ_NORMAL);
    // Trigger source off: no hardware trigger, peak detect still requested.
    setup();
    scope_acquisition = SCOPE_HW_ACQ_PEAK;
    scope_ch_enabled[0] = 0;
    scope_hw_trigger_sync();
    assert(!hw_enabled && hw_acquisition == SCOPE_HW_ACQ_PEAK);
    scope_acquisition = SCOPE_HW_ACQ_NORMAL;
    scope_hw_trigger_sync();
    assert(hw_acquisition == SCOPE_HW_ACQ_NORMAL);
}

// Peak capture decode: pair 2k = minimum, 2k+1 = maximum of group k.
static void test_peak_measurements(void) {
    setup();
    scope_frame_valid = 1;
    for (uint16_t k = 0; k < SCOPE_SAMPLE_COUNT; ++k) {
        uint8_t is_max = (uint8_t)(k & 1u);
        scope_samples[k * 2u] = is_max ? 200u : 100u;          // CH1 band 100..200
        scope_samples[k * 2u + 1u] = 128u;                      // CH2 flat
    }
    scope_samples[1001u * 2u] = 250u;                           // a glitch in one group max
    scope_capture_peak = 1;
    scope_compute_stats();
    // Envelope: every stored value; averages: middle of each group.
    assert(scope_min_raw[0] == 100u && scope_max_raw[0] == 250u);
    assert(scope_avg_raw[0] == 150u);
    assert(scope_sq_sum_raw[0] < 2044u * 25u * 25u);
    scope_capture_peak = 0;
    scope_compute_stats();
    assert(scope_sq_sum_raw[0] > 2044u * 50u * 50u);

    // Band columns: the glitch shows in exactly the column that covers pair 1001.
    scope_capture_peak = 1;
    scope_trigger_locked = 1;
    scope_trigger_offset = 900u;
    uint16_t visible = scope_visible_sample_count();
    uint8_t found = 0;
    for (uint16_t x = 0; x + SCOPE_TRACE_STEP <= 300u; x = (uint16_t)(x + SCOPE_TRACE_STEP)) {
        uint8_t lo;
        uint8_t hi;
        uint16_t first = (uint16_t)(900u + (uint32_t)x * (visible - 1u) / 300u);
        uint16_t last = (uint16_t)(900u + (uint32_t)(x + SCOPE_TRACE_STEP) * (visible - 1u) / 300u);
        scope_band_raw(x, 0, 300u, &lo, &hi);
        assert(lo == 100u);
        if ((first & ~1u) <= 1001u && 1001u < (last > (first & ~1u) + 2u ? last : (first & ~1u) + 2u)) {
            assert(hi == 250u);
            found = 1;
        } else {
            assert(hi == 200u);
        }
    }
    assert(found);
    scope_capture_peak = 0;
    scope_trigger_locked = 0;
}

static void expect_counter(uint64_t mhz, const char *text) {
    char out[12];
    scope_format_counter_reading(out, mhz);
    if (strcmp(out, text) != 0) {
        fprintf(stderr, "counter %llu mHz: '%s', expected '%s'\n", (unsigned long long)mhz, out, text);
        assert(0);
    }
}

// FREQ shows the FPGA counter (four significant digits) for the channel it counts.
static void test_counter_readout(void) {
    char out[12];

    expect_counter(6943u, "6.943HZ");
    expect_counter(99996u, "100.0HZ");
    expect_counter(59999u, "60.00HZ");
    expect_counter(999949u, "999.9HZ");
    expect_counter(1000000u, "1.000KHZ");
    expect_counter(41666666u, "41.67KHZ");
    expect_counter(123456789u, "123.5KHZ");
    expect_counter(1000000000u, "1.000MHZ");
    expect_counter(12500000000ull, "12.50MHZ");

    setup();
    scope_frame_valid = 1;
    for (uint16_t k = 0; k < SCOPE_SAMPLE_COUNT; ++k) {
        scope_samples[k * 2u] = (k / 50u) & 1u ? 200u : 60u;
        scope_samples[k * 2u + 1u] = (k / 50u) & 1u ? 200u : 60u;
    }
    scope_compute_stats();
    scope_measure_hist_ready = 0;
    hw_counter_ch = 0;
    hw_counter_value = 41666666u;
    scope_measure_value_for(out, 0, SCOPE_MEASURE_FREQ);
    assert(strcmp(out, "41.67KHZ") == 0);
    // CH2 is not counted: software estimate (1 decimal, no counter digits)
    scope_measure_value_for(out, 1, SCOPE_MEASURE_FREQ);
    assert(strcmp(out, "41.67KHZ") != 0 && out[0] != 0);
    hw_counter_ch = 0xFFu;
    scope_measure_value_for(out, 0, SCOPE_MEASURE_FREQ);
    assert(strcmp(out, "41.67KHZ") != 0);
}

// Trigger types: menu times converted to samples of the current timebase.
static void test_trigger_types(void) {
    char out[12];

    setup();
    scope_hw_trigger_sync();
    assert(hw_trigger.type == SCOPE_HW_TYPE_EDGE);
    scope_trigger_type = SCOPE_HW_TYPE_PULSE;
    scope_pulse_cond = SCOPE_HW_PULSE_RANGE;
    scope_trig_time_idx[0] = SCOPE_TRIG_TIME_DEFAULT_1;      // 1 ms
    scope_trig_time_idx[1] = SCOPE_TRIG_TIME_DEFAULT_2;      // 10 ms
    scope_window_high = 200u;
    ui.scope_timebase = 15u;                                  // 5MS: ~200 us per sample pair
    uint32_t period = scope_sample_period_ns();
    assert(period > 190000u && period < 210000u);
    scope_hw_trigger_sync();
    assert(hw_trigger.type == SCOPE_HW_TYPE_PULSE && hw_trigger.pulse == SCOPE_HW_PULSE_RANGE &&
           hw_trigger.width_lo == (1000000u + period / 2u) / period &&
           hw_trigger.width_hi == (10000000u + period / 2u) / period && hw_trigger.level_high == 200u);
    // rounded to the nearest sample, at least one, at most 65535
    scope_trig_time_idx[0] = 0;                               // 100 ns
    assert(scope_trig_samples(0) == 1u);
    ui.scope_timebase = 4u;                                   // 1US: 40 ns per pair
    scope_trig_time_idx[0] = SCOPE_TRIG_TIME_COUNT - 1u;      // 2 s
    assert(scope_trig_samples(0) == 0xFFFFu);
    ui.scope_timebase = 11u;                                  // 200US: ~8 us per pair
    scope_trig_time_idx[0] = 7u;                              // 20 us = ~2.5 samples
    period = scope_sample_period_ns();
    assert(scope_trig_samples(0) == (20000u + period / 2u) / period);
    scope_format_trig_time(out, 100u);
    assert(strcmp(out, "100NS") == 0);
    scope_format_trig_time(out, 20000u);
    assert(strcmp(out, "20US") == 0);
    scope_format_trig_time(out, 500000000u);
    assert(strcmp(out, "500MS") == 0);
    scope_format_trig_time(out, 2000000000u);
    assert(strcmp(out, "2S") == 0);
    scope_trigger_type = SCOPE_HW_TYPE_EDGE;
    scope_trig_time_idx[0] = SCOPE_TRIG_TIME_DEFAULT_1;
}

static void expect_rows(const uint8_t *expected, uint8_t count) {
    uint8_t rows[SCOPE_TRIGGER_ROWS_MAX];

    assert(scope_trigger_rows(rows) == count);
    assert(memcmp(rows, expected, count) == 0);
}

// Trigger menu: TYPE first, only the chips of the current type, four on screen.
static void test_trigger_menu(void) {
    static const uint8_t edge[] = {SCOPE_PARAM_TRIG_KIND, SCOPE_PARAM_TRIG_SOURCE, SCOPE_PARAM_TRIG_EDGE,
                                   SCOPE_PARAM_TRIG_LEVEL, SCOPE_PARAM_TRIG_TYPE};
    static const uint8_t pulse[] = {SCOPE_PARAM_TRIG_KIND, SCOPE_PARAM_TRIG_SOURCE, SCOPE_PARAM_TRIG_EDGE,
                                    SCOPE_PARAM_TRIG_LEVEL, SCOPE_PARAM_TRIG_WIDTH, SCOPE_PARAM_TRIG_T1,
                                    SCOPE_PARAM_TRIG_TYPE};
    static const uint8_t range[] = {SCOPE_PARAM_TRIG_KIND, SCOPE_PARAM_TRIG_SOURCE, SCOPE_PARAM_TRIG_EDGE,
                                    SCOPE_PARAM_TRIG_LEVEL, SCOPE_PARAM_TRIG_WIDTH, SCOPE_PARAM_TRIG_T1,
                                    SCOPE_PARAM_TRIG_T2, SCOPE_PARAM_TRIG_TYPE};
    static const uint8_t timeout[] = {SCOPE_PARAM_TRIG_KIND, SCOPE_PARAM_TRIG_SOURCE, SCOPE_PARAM_TRIG_EDGE,
                                      SCOPE_PARAM_TRIG_LEVEL, SCOPE_PARAM_TRIG_T1, SCOPE_PARAM_TRIG_TYPE};
    static const uint8_t window[] = {SCOPE_PARAM_TRIG_KIND, SCOPE_PARAM_TRIG_SOURCE, SCOPE_PARAM_TRIG_LEVEL,
                                     SCOPE_PARAM_TRIG_HIGH, SCOPE_PARAM_TRIG_TYPE};

    setup();
    scope_trigger_type = SCOPE_HW_TYPE_EDGE;
    scope_pulse_cond = SCOPE_HW_PULSE_GT;
    expect_rows(edge, sizeof(edge));
    scope_open_trigger_menu(SCOPE_PARAM_TRIG_TYPE);
    assert(ui.scope_trigger_menu && ui.scope_param == SCOPE_PARAM_TRIG_KIND);
    assert(strcmp(scope_param_label(SCOPE_PARAM_TRIG_EDGE), "EDGE") == 0);
    assert(strcmp(scope_param_value(SCOPE_PARAM_TRIG_EDGE), "RISE") == 0);
    // F1 F2 F4 sit over TYPE SRC LEVEL; MODE scrolls in from the right
    assert(scope_trigger_slot_param(0, 0) == SCOPE_PARAM_TRIG_KIND);
    assert(scope_trigger_slot_param(1, 0) == SCOPE_PARAM_TRIG_SOURCE);
    assert(scope_trigger_slot_param(3, 0) == SCOPE_PARAM_TRIG_LEVEL);
    for (uint8_t i = 0; i < 4u; ++i) {
        scope_step_param(1);
    }
    assert(ui.scope_param == SCOPE_PARAM_TRIG_TYPE);
    assert(scope_trigger_slot_param(0, 0) == SCOPE_PARAM_TRIG_SOURCE);
    assert(scope_trigger_slot_param(3, 0) == SCOPE_PARAM_TRIG_TYPE);
    scope_step_param(1);
    assert(ui.scope_param == SCOPE_PARAM_TRIG_KIND);
    assert(scope_trigger_slot_param(0, 0) == SCOPE_PARAM_TRIG_KIND);

    // TYPE: up/down cycles EDGE, PULSE, TIMEOUT, WINDOW
    scope_adjust_selected_param(1);
    assert(scope_trigger_type == SCOPE_HW_TYPE_PULSE);
    assert(strcmp(scope_param_value(SCOPE_PARAM_TRIG_KIND), "PULSE") == 0);
    assert(strcmp(scope_param_label(SCOPE_PARAM_TRIG_EDGE), "POL") == 0);
    assert(strcmp(scope_param_value(SCOPE_PARAM_TRIG_EDGE), "POS") == 0);
    expect_rows(pulse, sizeof(pulse));
    ui.scope_param = SCOPE_PARAM_TRIG_WIDTH;
    assert(strcmp(scope_param_value(SCOPE_PARAM_TRIG_WIDTH), "LONG") == 0);
    scope_adjust_selected_param(1);
    assert(scope_pulse_cond == SCOPE_HW_PULSE_RANGE);
    assert(strcmp(scope_param_value(SCOPE_PARAM_TRIG_WIDTH), "RANGE") == 0);
    expect_rows(range, sizeof(range));
    ui.scope_param = SCOPE_PARAM_TRIG_T2;
    scope_trig_time_idx[1] = SCOPE_TRIG_TIME_DEFAULT_2;
    assert(strcmp(scope_param_value(SCOPE_PARAM_TRIG_T2), "10MS") == 0);
    scope_adjust_selected_param(1);
    assert(strcmp(scope_param_value(SCOPE_PARAM_TRIG_T2), "20MS") == 0);
    ui.scope_param = SCOPE_PARAM_TRIG_T1;
    scope_trig_time_idx[0] = 0;
    scope_adjust_selected_param(-1);
    assert(scope_trig_time_idx[0] == 0 && strcmp(scope_param_value(SCOPE_PARAM_TRIG_T1), "100NS") == 0);
    scope_trig_time_idx[0] = SCOPE_TRIG_TIME_COUNT - 1u;
    scope_adjust_selected_param(1);
    assert(scope_trig_time_idx[0] == SCOPE_TRIG_TIME_COUNT - 1u);
    assert(scope_trigger_slot_param(3, 0) == SCOPE_PARAM_TRIG_T1);
    ui.scope_param = SCOPE_PARAM_TRIG_KIND;
    scope_adjust_selected_param(1);
    assert(scope_trigger_type == SCOPE_HW_TYPE_TIMEOUT);
    expect_rows(timeout, sizeof(timeout));
    scope_adjust_selected_param(1);
    assert(scope_trigger_type == SCOPE_HW_TYPE_WINDOW);
    expect_rows(window, sizeof(window));
    assert(strcmp(scope_param_label(SCOPE_PARAM_TRIG_LEVEL), "LOW") == 0);
    assert(scope_trigger_slot_param(3, 0) == SCOPE_PARAM_TRIG_HIGH);
    // window high: like LEVEL, never outside the codes the comparator uses
    ui.scope_param = SCOPE_PARAM_TRIG_HIGH;
    scope_window_high = 242u;
    scope_adjust_selected_param(1);
    scope_adjust_selected_param(1);
    assert(scope_window_high == 243u);
    // chips that left with a type change are not kept selected
    scope_trigger_type = SCOPE_HW_TYPE_EDGE;
    scope_sanitize_state();
    assert(ui.scope_param == SCOPE_PARAM_TRIG_TYPE);
    scope_trigger_type = SCOPE_HW_TYPE_EDGE;
    scope_pulse_cond = SCOPE_HW_PULSE_GT;
    scope_trig_time_idx[0] = SCOPE_TRIG_TIME_DEFAULT_1;
    scope_trig_time_idx[1] = SCOPE_TRIG_TIME_DEFAULT_2;
    scope_window_high = 192u;
    scope_clear_menus();
}

// ACQUIRE lives in the CH1 long press menu; the trigger status sits in the top bar.
static void test_acquire_and_status(void) {
    uint8_t rows = sizeof(math_menu_order) / sizeof(math_menu_order[0]);

    setup();
    assert(math_menu_order[rows - 1u] == MENU_ITEM_ACQUIRE);
    scope_acquisition = SCOPE_HW_ACQ_NORMAL;
    scope_cycle_acquisition(1);
    assert(scope_acquisition == SCOPE_HW_ACQ_PEAK);
    scope_cycle_acquisition(1);
    assert(scope_acquisition == SCOPE_HW_ACQ_NORMAL);
    scope_cycle_acquisition(-1);
    assert(scope_acquisition == SCOPE_HW_ACQ_PEAK);
    scope_acquisition = SCOPE_HW_ACQ_NORMAL;

    setup();
    ui.running = 1;
    scope_frame_valid = 1;
    scope_frame_age_ms = 0;
    scope_trigger_locked = 1;
    assert(scope_trigger_state() == SCOPE_TRIG_STATE_TRIGGERED);
    scope_frame_age_ms = 5000u;
    assert(scope_trigger_state() == SCOPE_TRIG_STATE_WAIT);
    ui.scope_trigger_mode = SCOPE_TRIGGER_AUTO;
    assert(scope_trigger_state() == SCOPE_TRIG_STATE_AUTO);
    scope_frame_age_ms = 0;
    scope_trigger_locked = 0;
    assert(scope_trigger_state() == SCOPE_TRIG_STATE_AUTO);
    ui.scope_trigger_mode = SCOPE_TRIGGER_SINGLE;
    assert(scope_trigger_state() == SCOPE_TRIG_STATE_WAIT);
    ui.running = 0;
    assert(scope_trigger_state() == SCOPE_TRIG_STATE_STOP);
    ui.running = 1;
    ui.scope_display = SCOPE_DISPLAY_XY;
    assert(scope_trigger_state() == SCOPE_TRIG_STATE_AUTO);
    ui.scope_display = SCOPE_DISPLAY_YT;
    // slow timebases: a capture alone takes longer than a second
    ui.scope_timebase = 20u;                                  // 200MS, the slowest block capture
    scope_trigger_locked = 1;
    scope_frame_age_ms = 5000u;
    assert(scope_trigger_state() == SCOPE_TRIG_STATE_TRIGGERED);
    scope_frame_age_ms = 6000u;
    assert(scope_trigger_state() == SCOPE_TRIG_STATE_WAIT);
    // slow roll has no trigger: free running
    ui.scope_timebase = SCOPE_SLOW_ROLL_TIMEBASE_START;
    assert(scope_trigger_state() == SCOPE_TRIG_STATE_AUTO);
    scope_trigger_locked = 0;
    scope_frame_valid = 0;
    assert(strcmp(scope_acquisition_badge() ? scope_acquisition_badge() : "", "") == 0);
    scope_acquisition = SCOPE_HW_ACQ_PEAK;
    assert(strcmp(scope_acquisition_badge(), "PEAK") == 0);
    scope_acquisition = SCOPE_HW_ACQ_NORMAL;
}

int main(void) {
    test_settings();
    test_window();
    test_peak();
    test_peak_measurements();
    test_counter_readout();
    test_trigger_types();
    test_trigger_menu();
    test_acquire_and_status();
    puts("scope hardware trigger mapping, peak detect, counter readout and trigger type tests passed");
    return 0;
}
