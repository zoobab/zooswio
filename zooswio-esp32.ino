// CH32V003 SWIO Programmer for ESP32
// Adapted from zoobab/zooswio (ATmega328P version)
//
// Key differences vs ATmega328P version:
//   - Direct AVR port registers (DDRB/PORTB/PINB/_BV) replaced with ESP32 GPIO register macros
//   - NOP counts scaled for ESP32 @ 240MHz (ATmega @ 16MHz: 1 nop ~62.5ns, ESP32: 1 nop ~4.2ns)
//     -> ~15 nops on ESP32 ≈ 1 nop on ATmega328P (62.5ns / 4.2ns ≈ 14.9)
//   - Pin numbers changed to ESP32 GPIO numbers (edit as needed)

#include <Arduino.h>
#include "driver/gpio.h"

// ---------------------------------------------------------------------------
// Pin definitions — edit these to match your wiring
// ---------------------------------------------------------------------------
#define SWIO_PIN_NUM      4   // GPIO4
#define TARGET_POWER_PIN  5   // GPIO5

// ---------------------------------------------------------------------------
// ESP32 fast GPIO helpers (direct register access, interrupt-safe alternative
// to digitalWrite which is too slow for SWIO timing)
// ---------------------------------------------------------------------------
#define SWIO_MASK         (1UL << SWIO_PIN_NUM)

// Drive pin LOW (output mode, low)
#define SWIO_OUTPUT_LOW() do { \
    GPIO.enable_w1ts = SWIO_MASK; \
    GPIO.out_w1tc    = SWIO_MASK; \
} while(0)

// Drive pin HIGH (output mode, high)
#define SWIO_OUTPUT_HIGH() do { \
    GPIO.enable_w1ts = SWIO_MASK; \
    GPIO.out_w1ts    = SWIO_MASK; \
} while(0)

// Release pin to input (tri-state / floating)
#define SWIO_INPUT() do { \
    GPIO.enable_w1tc = SWIO_MASK; \
} while(0)

// Read pin
#define SWIO_READ()  ((GPIO.in >> SWIO_PIN_NUM) & 1)

// ---------------------------------------------------------------------------
// NOP timing helpers
// ESP32 @ 240MHz: 1 cycle ≈ 4.17ns
// ATmega @ 16MHz: 1 cycle ≈ 62.5ns  → ratio ≈ 15×
// The original uses ~250ns "T" blocks. We approximate here:
//   T_half  ≈  2×nop on AVR = 125ns  → ~30 nops on ESP32
//   T_full  ≈  4×nop on AVR = 250ns  → ~60 nops on ESP32
//
// IMPORTANT: if you use a different CPU frequency (e.g. 160MHz) adjust the
// counts below proportionally.  At 160MHz: 1 cycle ≈ 6.25ns → ratio ≈ 10×.
// ---------------------------------------------------------------------------
#define NOP1()  asm volatile("nop")
#define NOP15() do { \
    NOP1();NOP1();NOP1();NOP1();NOP1(); \
    NOP1();NOP1();NOP1();NOP1();NOP1(); \
    NOP1();NOP1();NOP1();NOP1();NOP1(); \
} while(0)
#define NOP30() do { NOP15(); NOP15(); } while(0)
#define NOP60() do { NOP30(); NOP30(); } while(0)

// ---------------------------------------------------------------------------
// Protocol commands (unchanged)
// ---------------------------------------------------------------------------
#define PROTOCOL_START     '!'
#define PROTOCOL_ACK       '+'
#define PROTOCOL_TEST      '?'
#define PROTOCOL_POWER_ON  'p'
#define PROTOCOL_POWER_OFF 'P'
#define PROTOCOL_WRITE_REG 'w'
#define PROTOCOL_READ_REG  'r'

// ---------------------------------------------------------------------------
// SWIO bit primitives
// Timing reference: SWIO uses ~250ns "T" blocks (one logic unit).
//   send_one:  pull low 1T, release high 3T
//   send_zero: pull low 3T, release high 1T (or longer)
//   recv_bit:  pull low 1T, precharge, release, sample, wait for line to recover
// ---------------------------------------------------------------------------

static inline IRAM_ATTR void swio_send_one() {
    // Pull low ~1T (≈ 250ns)
    SWIO_OUTPUT_LOW();
    NOP60();
    // Drive high ~3T then release
    SWIO_OUTPUT_HIGH();
    NOP30();
    SWIO_INPUT();
}

static inline IRAM_ATTR void swio_send_zero() {
    // Pull low ~3T (≈ 750ns)
    SWIO_OUTPUT_LOW();
    NOP60(); NOP60(); NOP30(); // ≈ 3× 250ns
    // Drive high 1T then release
    SWIO_OUTPUT_HIGH();
    NOP30();
    SWIO_INPUT();
}

static inline IRAM_ATTR char swio_recv_bit() {
    // Pull low ~1T
    SWIO_OUTPUT_LOW();
    NOP30();
    // Precharge line high
    SWIO_OUTPUT_HIGH();
    NOP15();
    // Release to input and wait a moment before sampling
    SWIO_INPUT();
    NOP30(); NOP30(); // ≈ 2× half-T settling time
    // Sample
    char x = (char)SWIO_READ();
    // Wait for line to come back up if driven low by target
    uint32_t timeout = 100000;
    while (!SWIO_READ() && --timeout)
        ;
    return x;
}

// ---------------------------------------------------------------------------
// SWIO register access
// ---------------------------------------------------------------------------
void IRAM_ATTR swio_write_reg(uint8_t addr, uint32_t val) {
    // Start bit
    swio_send_one();

    // Address (7 bits, MSB first)
    for (int i = 0; i < 7; i++) {
        if (addr & 0x40)
            swio_send_one();
        else
            swio_send_zero();
        addr <<= 1;
    }

    // Write indicator (1 = write)
    swio_send_one();

    // Data (32 bits, MSB first)
    for (int i = 0; i < 32; i++) {
        if (val & 0x80000000UL)
            swio_send_one();
        else
            swio_send_zero();
        val <<= 1;
    }

    // Stop / idle
    delayMicroseconds(10);
}

uint32_t IRAM_ATTR swio_read_reg(uint8_t addr) {
    uint32_t x = 0;

    // Start bit
    swio_send_one();

    // Address (7 bits, MSB first)
    for (int i = 0; i < 7; i++) {
        if (addr & 0x40)
            swio_send_one();
        else
            swio_send_zero();
        addr <<= 1;
    }

    // Read indicator (0 = read)
    swio_send_zero();

    // Data (32 bits, MSB first)
    for (int i = 0; i < 32; i++) {
        x <<= 1;
        if (swio_recv_bit())
            x |= 1;
    }

    // Stop / idle
    delayMicroseconds(10);

    return x;
}

void swio_init() {
    // Initialise SWIO pin as output-high, then release as per protocol
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = SWIO_MASK;
    io_conf.mode         = GPIO_MODE_INPUT_OUTPUT_OD; // open-drain + input for reading back
    io_conf.pull_up_en   = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    // Drive high for 5ms, then pull low for 20ms (reset pulse), then release
    SWIO_OUTPUT_HIGH();
    delay(5);
    SWIO_OUTPUT_LOW();
    delay(20);
    SWIO_OUTPUT_HIGH();
    SWIO_INPUT();
}

void target_power(int on) {
    digitalWrite(TARGET_POWER_PIN, on ? HIGH : LOW);
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);

    // Target power pin
    pinMode(TARGET_POWER_PIN, OUTPUT);
    digitalWrite(TARGET_POWER_PIN, LOW);

    // SWIO initialisation
    swio_init();

    // Signal ready to host
    Serial.write(PROTOCOL_START);
}

void loop() {
    if (Serial.available() > 0) {
        char cmd = (char)Serial.read();
        uint8_t  reg = 0;
        uint32_t val = 0;

        switch (cmd) {
            case PROTOCOL_TEST:
                Serial.write(PROTOCOL_ACK);
                break;

            case PROTOCOL_POWER_ON:
                target_power(1);
                Serial.write(PROTOCOL_ACK);
                break;

            case PROTOCOL_POWER_OFF:
                target_power(0);
                Serial.write(PROTOCOL_ACK);
                break;

            case PROTOCOL_WRITE_REG:
                while (Serial.available() < 1);
                Serial.readBytes((char*)&reg, sizeof(uint8_t));
                while (Serial.available() < 4);
                Serial.readBytes((char*)&val, sizeof(uint32_t));
                swio_write_reg(reg, val);
                Serial.write(PROTOCOL_ACK);
                break;

            case PROTOCOL_READ_REG:
                while (Serial.available() < 1);
                Serial.readBytes((char*)&reg, sizeof(uint8_t));
                val = swio_read_reg(reg);
                Serial.write((uint8_t*)&val, sizeof(uint32_t));
                break;

            default:
                // Ignore unknown commands
                break;
        }
    }
}
