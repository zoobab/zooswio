// CH32V003 SWIO Programmer for ESP32
// Ported from ATmega328P (zoobab/zooswio) to ESP32
//
// Key differences from ATmega328P version:
//   - Direct AVR port manipulation (DDRB/PORTB/PINB/_BV) replaced with ESP32 GPIO API
//   - NOP timing recalibrated: ATmega @16MHz ~62.5ns/nop, ESP32 @240MHz ~4ns/nop
//     -> each "T" slot (~250ns) now requires ~60 NOPs instead of ~2
//   - gpio_set_direction / gpio_set_level / gpio_get_level used for bit-banging
//   - Pin numbers changed to GPIO numbers suitable for ESP32 (3.3V logic, matches CH32V003)
//
// Wiring (same protocol as BlueSyncLine):
//   GPIO 4  -> SWIO (single-wire debug I/O to CH32V003)
//   GPIO 5  -> TARGET_POWER (optional, controls target VCC via transistor/MOSFET)
//
// Flash with: arduino-cli compile --fqbn esp32:esp32:esp32 zooswio_esp32
//             arduino-cli upload  --fqbn esp32:esp32:esp32 -p /dev/ttyUSB0 zooswio_esp32

#include <Arduino.h>
#include "driver/gpio.h"

// ── Pin definitions ──────────────────────────────────────────────────────────
#define SWIO_PIN_NUM      4   // GPIO4 - SWIO data line
#define TARGET_POWER_PIN  5   // GPIO5 - target power control

// ── Protocol commands (unchanged from original) ──────────────────────────────
#define PROTOCOL_START     '!'
#define PROTOCOL_ACK       '+'
#define PROTOCOL_TEST      '?'
#define PROTOCOL_POWER_ON  'p'
#define PROTOCOL_POWER_OFF 'P'
#define PROTOCOL_WRITE_REG 'w'
#define PROTOCOL_READ_REG  'r'

// ── Timing helpers ───────────────────────────────────────────────────────────
// ESP32 default CPU: 240 MHz -> 1 cycle = ~4.17 ns
// One SWIO "T" slot ≈ 250 ns -> ~60 CPU cycles.
// We use NOP macros to burn cycles. Adjust NOP_PER_T if your ESP32 runs at
// a different frequency (80 or 160 MHz are common alternatives).
//
// At 240 MHz: 60 nops ≈ 250 ns  ✓
// At 160 MHz: 40 nops ≈ 250 ns
// At  80 MHz: 20 nops ≈ 250 ns

#define NOP1  __asm__ __volatile__("nop")
#define NOP4  NOP1; NOP1; NOP1; NOP1
#define NOP16 NOP4; NOP4; NOP4; NOP4
#define NOP60 NOP16; NOP16; NOP16; NOP4; NOP4; NOP4

// One half-T pause (between transitions within a bit cell)
#define HALF_T NOP60

// ── Fast GPIO inlines ────────────────────────────────────────────────────────
// Using the IDF low-level macros for speed.  These expand to direct register
// writes and are safe to call from IRAM (no cache misses).

static inline void IRAM_ATTR swio_set_output() {
    gpio_set_direction((gpio_num_t)SWIO_PIN_NUM, GPIO_MODE_OUTPUT);
}

static inline void IRAM_ATTR swio_set_input() {
    // Input with internal pull-up, matching the open-drain SWIO idle state
    gpio_set_direction((gpio_num_t)SWIO_PIN_NUM, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)SWIO_PIN_NUM, GPIO_PULLUP_ONLY);
}

static inline void IRAM_ATTR swio_high() {
    gpio_set_level((gpio_num_t)SWIO_PIN_NUM, 1);
}

static inline void IRAM_ATTR swio_low() {
    gpio_set_level((gpio_num_t)SWIO_PIN_NUM, 0);
}

static inline int IRAM_ATTR swio_read() {
    return gpio_get_level((gpio_num_t)SWIO_PIN_NUM);
}

// ── SWIO bit primitives ──────────────────────────────────────────────────────
//
// Bit encoding (same as original, from ch32fun/minichlink ardulink protocol):
//   ONE  : drive LOW for ~1T, then HIGH for ~3T (short low pulse)
//   ZERO : drive LOW for ~7T, then HIGH for ~2T (long low pulse)
//   RECV : drive LOW for ~1T, precharge HIGH, release, sample at ~5T
//
// Original ATmega code used 2 NOPs per T at 16 MHz.
// Here we use NOP60 per T at 240 MHz.

static inline void IRAM_ATTR swio_send_one() {
    swio_set_output();
    swio_low();           // T0: drive low
    HALF_T;               // T1: hold low ~1T
    swio_high();          // T1->T2: go high early (short pulse = '1')
    HALF_T; HALF_T;       // T2-T3: hold high
    swio_set_input();     // T4: release (open-drain idle)
}

static inline void IRAM_ATTR swio_send_zero() {
    swio_set_output();
    swio_low();           // T0: drive low
    HALF_T; HALF_T;       // T1-T2
    HALF_T; HALF_T;       // T3-T4
    HALF_T; HALF_T;       // T5-T6
    HALF_T;               // T7: hold low ~7T (long pulse = '0')
    swio_high();          // T7->T8: go high
    HALF_T;               // T8: brief high before release
    swio_set_input();     // T9: release
}

static inline char IRAM_ATTR swio_recv_bit() {
    int x;
    swio_set_output();
    swio_low();           // T0: drive low (initiate read)
    HALF_T;               // T1
    swio_high();          // T2: precharge line before releasing
    swio_set_input();     // T3: release - target drives line
    HALF_T; HALF_T;       // T4-T5: wait for target to drive its response
    x = swio_read();      // T6: sample

    // Wait for line to return high (open-drain release by target)
    while (!swio_read());

    return (char)x;
}

// ── Register-level SWIO transactions ────────────────────────────────────────

void IRAM_ATTR swio_write_reg(uint8_t addr, uint32_t val) {
    // Start bit
    swio_send_one();

    // 7-bit address (MSB first)
    for (int i = 0; i < 7; i++) {
        if (addr & 0x40)
            swio_send_one();
        else
            swio_send_zero();
        addr <<= 1;
    }

    // Direction bit: 1 = write
    swio_send_one();

    // 32-bit value (MSB first)
    for (int i = 0; i < 32; i++) {
        if (val & 0x80000000)
            swio_send_one();
        else
            swio_send_zero();
        val <<= 1;
    }

    // Stop: idle the bus
    delayMicroseconds(10);
}

uint32_t IRAM_ATTR swio_read_reg(uint8_t addr) {
    uint32_t x = 0;

    // Start bit
    swio_send_one();

    // 7-bit address (MSB first)
    for (int i = 0; i < 7; i++) {
        if (addr & 0x40)
            swio_send_one();
        else
            swio_send_zero();
        addr <<= 1;
    }

    // Direction bit: 0 = read
    swio_send_zero();

    // Receive 32-bit value (MSB first)
    for (int i = 0; i < 32; i++) {
        x <<= 1;
        if (swio_recv_bit())
            x |= 1;
    }

    // Stop: idle the bus
    delayMicroseconds(10);
    return x;
}

void swio_init() {
    // Pull line high (idle), then pulse low to reset SWIO state machine
    gpio_reset_pin((gpio_num_t)SWIO_PIN_NUM);
    swio_set_output();
    swio_high();
    delay(5);
    swio_low();
    delay(20);
    swio_high();
    swio_set_input();   // Release to open-drain mode
}

// ── Target power ─────────────────────────────────────────────────────────────

void target_power(int on) {
    digitalWrite(TARGET_POWER_PIN, on ? HIGH : LOW);
}

// ── Arduino setup/loop ───────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);

    // Target power pin
    pinMode(TARGET_POWER_PIN, OUTPUT);
    digitalWrite(TARGET_POWER_PIN, LOW);

    // SWIO init
    swio_init();

    // Signal ready (same as original ardulink protocol)
    Serial.write(PROTOCOL_START);
}

void loop() {
    if (Serial.available() > 0) {
        char cmd = Serial.read();
        uint8_t reg;
        uint32_t val;

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
        }
    }
}
