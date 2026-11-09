// Host regression test: UI trigger settings -> FPGA hardware trigger (FPGA_HW_TRIGGER=1).
#include "../src/ui.c"
#include "../src/fpga.h"

#include <assert.h>
#include <stdio.h>

static uint8_t hw_enabled;
static uint8_t hw_mode;
static uint8_t hw_source;
static uint8_t hw_falling;
static uint8_t hw_level;
static uint16_t hw_pre;
static uint8_t hw_hit;
static uint16_t hw_hit_pre;

void scope_hw_set_trigger(uint8_t enabled,
                          uint8_t mode,
                          uint8_t source,
                          uint8_t falling,
                          uint8_t level,
                          uint16_t pre_samples) {
    hw_enabled = enabled;
    hw_mode = mode;
    hw_source = source;
    hw_falling = falling;
    hw_level = level;
    hw_pre = pre_samples;
}

uint8_t scope_hw_trigger_position(uint16_t *pre_samples) {
    if (!hw_hit) {
        return 0;
    }
    *pre_samples = hw_hit_pre;
    return 1;
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

int main(void) {
    test_settings();
    test_window();
    puts("scope hardware trigger mapping tests passed");
    return 0;
}
