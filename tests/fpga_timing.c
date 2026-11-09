#include "../src/hw.h"

#include <assert.h>
#include <stdio.h>

static struct { uint32_t address; uint32_t value; } registers[32];
static unsigned register_count;
static uint8_t transmitted[64];
static unsigned transmitted_count;
#if HW_TARGET_HW40
// Register file of the open bitstream: frame writes in mode 11, read-back in mode 01.
static uint8_t fpga_frame[16];
static unsigned fpga_index;
static unsigned fpga_last_mode = 4;
static uint8_t bus_stuck_low;
#endif

static volatile uint32_t *mock_register(uint32_t address) {
    for (unsigned i = 0; i < register_count; ++i) {
        if (registers[i].address == address) return &registers[i].value;
    }
    assert(register_count < 32);
    registers[register_count].address = address;
    return &registers[register_count++].value;
}

#undef REG32
#define REG32(address) (*mock_register(address))

static void mock_gpio_set(uint32_t base, uint32_t mask) {
    GPIO_ODR(base) |= mask;
#if HW_TARGET_HW40
    if (base == GPIOB_BASE && mask == (1u << 3)) {
        unsigned mode = ((GPIO_ODR(GPIOB_BASE) >> 5) & 1u) << 1 | ((GPIO_ODR(GPIOA_BASE) >> 15) & 1u);
        assert(transmitted_count < sizeof(transmitted));
        transmitted[transmitted_count++] = (uint8_t)GPIO_ODR(GPIOC_BASE);
        fpga_index = mode == fpga_last_mode ? fpga_index + 1u : 0u;
        fpga_last_mode = mode;
        if (mode == 3u) {
            fpga_frame[fpga_index & 15u] = (uint8_t)(GPIO_ODR(GPIOC_BASE) & ~bus_stuck_low);
        } else if (mode == 1u) {
            GPIO_IDR(GPIOC_BASE) = (uint8_t)(fpga_frame[fpga_index & 15u] & ~bus_stuck_low);
        }
    }
#else
    if (base == GPIOA_BASE && mask == (1u << 15)) {
        assert(transmitted_count < sizeof(transmitted));
        transmitted[transmitted_count++] = (uint8_t)SPI_DT(SPI3_BASE);
    }
#endif
}
static void mock_gpio_clear(uint32_t base, uint32_t mask) { GPIO_ODR(base) &= ~mask; }
static uint32_t mock_gpio_read(uint32_t base, uint32_t mask) { return GPIO_IDR(base) & mask; }
#define gpio_set mock_gpio_set
#define gpio_clear mock_gpio_clear
#define gpio_read mock_gpio_read
#include "../src/fpga.c"

void gpio_config_mask(uint32_t base, uint16_t mask, uint8_t cfg) {
    (void)base; (void)mask; (void)cfg;
}

static uint32_t read_be32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
           (uint32_t)bytes[2] << 8 | bytes[3];
}

static void assert_frame(uint32_t word, uint32_t span) {
    unsigned offset = HW_TARGET_HW40 ? 0u : 1u;
    assert(transmitted_count == (HW_TARGET_HW40 ? 16u : 9u));
    assert(read_be32(&transmitted[offset]) == word);
    assert(read_be32(&transmitted[offset + 4]) == span);
    assert(fpga_last_tuning_word == word && fpga_last_span == span);
#if HW_TARGET_HW40
    for (unsigned i = 0; i < 8; ++i) assert(transmitted[8 + i] == i);
#else
    assert(transmitted[0] == 0);
#endif
    transmitted_count = 0;
}

int main(void) {
    SPI_STS(SPI3_BASE) = 2;
    fpga_write_generator_timing(42950);
    assert_frame(42950, FPGA_SAMPLE_COUNT);
    static const uint32_t spans[] = {0, 52, 52428, 0x80000, 0x100000};
    for (unsigned i = 0; i < sizeof(spans) / sizeof(spans[0]); ++i) {
        fpga_write_scope_timing(spans[i]);
        assert_frame(42950, spans[i]);
        fpga_write_generator_timing(85900);
        assert_frame(85900, spans[i]);
        fpga_write_generator_timing(0);
        assert_frame(0, spans[i]);
        fpga_write_generator_timing(42950);
        assert_frame(42950, spans[i]);
    }
#if HW_TARGET_HW40
    static const uint8_t trigger_tail[8] = {0x54, 0x0Au, 128u, 3u, 0x03u, 0xFFu, 50u, 7u};
    fpga_set_trigger_tail(trigger_tail);
    fpga_write_scope_timing(0x100000);
    assert(transmitted_count == 16u);
    for (unsigned i = 0; i < 8; ++i) assert(transmitted[8 + i] == trigger_tail[i]);
    transmitted_count = 0;
    fpga_set_trigger_tail(0);
    fpga_write_scope_timing(0x100000);
    assert_frame(42950, 0x100000);
#if FPGA_OPEN_BITSTREAM
    // Bus self-test: a clean bus, then PC7 (D7, pin 13) stuck low.
    fpga_loaded = 1u;
    fpga_bus_test_run();
    const fpga_bus_test_t *bt = fpga_bus_test_result();
    assert(bt->done && bt->bad_bits == 0);
    assert(bt->sent[8] == 0x80u && bt->read[9] == 0x7Fu && bt->read[4] == 0x00u && bt->read[5] == 0x10u);
    // The last frame restores the normal tail.
    for (unsigned i = 0; i < 8; ++i) assert(fpga_frame[8 + i] == i);
    transmitted_count = 0;
    bus_stuck_low = 0x80u;
    fpga_bus_test_run();
    assert(bt->bad_bits == 0x80u);
    bus_stuck_low = 0;
    transmitted_count = 0;
    assert(FPGA_FAST_CLOCK_HZ == 50000000u);
#endif
#endif
    printf("FPGA timing: independent DDS/capture fields and hardware packet framing passed (HW4=%d)\n", HW_TARGET_HW40);
    return 0;
}
