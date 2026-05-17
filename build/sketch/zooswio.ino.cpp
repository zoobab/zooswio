#line 1 "/home/zoobab/soft/zooswio/zooswio.ino"
// CH32V003 SWIO Programmer for RP2040 (Raspberry Pi Pico)
// Ported from ESP32 (zoobab/zooswio) to RP2040
//
// Key differences from ESP32 version:
//   - ESP32 GPIO API (gpio_set_level/gpio_get_level) replaced with RP2040 SIO registers
//   - NOP timing recalibrated: ESP32 @240MHz ~4ns/nop, RP2040 @125MHz ~8ns/nop
//     -> each "T" slot (~250ns) now requires ~31 NOPs instead of ~60
//   - Direct SIO register access (sio_hw->gpio_oe_set/clr, gpio_out_set/clr, gpio_in)
//     for timing-critical bit-banging
//   - Pin numbers changed to RP2040 GPIO numbers (3.3V logic, matches CH32V003)
//
// Wiring (same protocol as BlueSyncLine):
//   GP4  -> SWIO (single-wire debug I/O to CH32V003)
//   GP5  -> TARGET_POWER (optional, controls target VCC via transistor/MOSFET)
//
// Board: Raspberry Pi Pico (Earle Philhower's arduino-pico core)
// Flash with: arduino-cli compile --fqbn rp2040:rp2040:raspberry-pi-pico zooswio
//             arduino-cli upload  --fqbn rp2040:rp2040:raspberry-pi-pico -p /dev/ttyACM0 zooswio

#include <Arduino.h>
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"

// ── Pin definitions ──────────────────────────────────────────────────────────
#define SWIO_PIN_NUM      4   // GP4 - SWIO data line
#define TARGET_POWER_PIN  5   // GP5 - target power control
#define SWIO_BIT          (1u << SWIO_PIN_NUM)

// ── Protocol commands (unchanged from original) ──────────────────────────────
#define PROTOCOL_START     '!'
#define PROTOCOL_ACK       '+'
#define PROTOCOL_TEST      '?'
#define PROTOCOL_POWER_ON  'p'
#define PROTOCOL_POWER_OFF 'P'
#define PROTOCOL_WRITE_REG 'w'
#define PROTOCOL_READ_REG  'r'

// ── Timing helpers ──────────────────────────────────────────────────────
// RP2040 default CPU: 125 MHz -> 1 cycle = ~8 ns
// One SWIO "T" slot ≈ 250 ns -> ~31 CPU cycles.
// We use NOP macros to burn cycles.
//
// At 125 MHz: 32 nops ≈ 256 ns  ✓  (~1T)
//
// SIO register writes (gpio_oe_set/clr, gpio_out_set/clr) take ~2 cycles each,
// so NOP counts are adjusted to keep the total between-state time near 250 ns.

#define NOP1  __asm__ __volatile__("nop")
#define NOP4  NOP1; NOP1; NOP1; NOP1
#define NOP8  NOP4; NOP4
#define NOP16 NOP8; NOP8
#define NOP32 NOP16; NOP16

// One half-T pause (between transitions within a bit cell)
#define HALF_T NOP16  // ~128 ns
// One full-T pause
#define ONE_T  NOP32  // ~256 ns

// ── Fast GPIO inlines ──────────────────────────────────────────────────────
// Using direct SIO register writes for speed. These are single STR instructions
// and execute in 2 cycles, suitable for bit-banging at 250 ns resolution.

#line 63 "/home/zoobab/soft/zooswio/zooswio.ino"
static void swio_set_output();
#line 67 "/home/zoobab/soft/zooswio/zooswio.ino"
static void swio_set_input();
#line 72 "/home/zoobab/soft/zooswio/zooswio.ino"
static void swio_high();
#line 76 "/home/zoobab/soft/zooswio/zooswio.ino"
static void swio_low();
#line 80 "/home/zoobab/soft/zooswio/zooswio.ino"
static int swio_read();
#line 91 "/home/zoobab/soft/zooswio/zooswio.ino"
static void swio_send_one();
#line 100 "/home/zoobab/soft/zooswio/zooswio.ino"
static void swio_send_zero();
#line 115 "/home/zoobab/soft/zooswio/zooswio.ino"
static char swio_recv_bit();
#line 134 "/home/zoobab/soft/zooswio/zooswio.ino"
void swio_write_reg(uint8_t addr, uint32_t val);
#line 163 "/home/zoobab/soft/zooswio/zooswio.ino"
uint32_t swio_read_reg(uint8_t addr);
#line 193 "/home/zoobab/soft/zooswio/zooswio.ino"
void swio_init();
#line 211 "/home/zoobab/soft/zooswio/zooswio.ino"
void target_power(int on);
#line 217 "/home/zoobab/soft/zooswio/zooswio.ino"
void setup();
#line 231 "/home/zoobab/soft/zooswio/zooswio.ino"
void loop();
#line 63 "/home/zoobab/soft/zooswio/zooswio.ino"
static inline void swio_set_output() {
    sio_hw->gpio_oe_set = SWIO_BIT;
}

static inline void swio_set_input() {
    sio_hw->gpio_oe_clr = SWIO_BIT;
    // Pull-up is set once during init
}

static inline void swio_high() {
    sio_hw->gpio_oe_set = SWIO_BIT;
}

static inline void swio_low() {
    sio_hw->gpio_oe_clr = SWIO_BIT;
}

static inline int swio_read() {
    return (sio_hw->gpio_in & SWIO_BIT) ? 1 : 0;
}

// ── SWIO bit primitives ──────────────────────────────────────────────────
//
// Bit encoding (same as original, from ch32fun/minichlink ardulink protocol):
//   ONE  : drive LOW for ~1T, then HIGH for ~3T (short low pulse)
//   ZERO : drive LOW for ~7T, then HIGH for ~2T (long low pulse)
//   RECV : drive LOW for ~1T, precharge HIGH, release, sample at ~5T

static inline void swio_send_one() {
    swio_set_output();
    swio_low();           // T0: drive low
    ONE_T;                // T1: hold low ~1T
    swio_high();          // T2: go high early (short pulse = '1')
    ONE_T; ONE_T;         // T3-T4: hold high
    swio_set_input();     // T5: release (open-drain idle)
}

static inline void swio_send_zero() {
    swio_set_output();
    swio_low();           // T0: drive low
    ONE_T;                // T1
    ONE_T;                // T2
    ONE_T;                // T3
    ONE_T;                // T4
    ONE_T;                // T5
    ONE_T;                // T6
    ONE_T;                // T7: hold low ~7T (long pulse = '0')
    swio_high();          // T7->T8: go high
    ONE_T;                // T8: brief high before release
    swio_set_input();     // T9: release
}

static inline char swio_recv_bit() {
    int x;
    swio_set_output();
    swio_low();           // T0: drive low (initiate read)
    HALF_T;               // T1
    swio_high();          // T2: precharge line before releasing
    swio_set_input();     // T3: release - target drives line
    ONE_T;                // T4-T5: wait for target to drive its response
    x = swio_read();      // T6: sample

    // Wait for line to return high (open-drain release by target)
    while (!swio_read())
        ;

    return (char)x;
}

// ── Register-level SWIO transactions ────────────────────────────────────

void swio_write_reg(uint8_t addr, uint32_t val) {
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

uint32_t swio_read_reg(uint8_t addr) {
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
    // Configure SWIO pin for SIO function with pull-up
    gpio_set_function(SWIO_PIN_NUM, GPIO_FUNC_SIO);
    gpio_pull_up(SWIO_PIN_NUM);

    // Pull line high (idle), then pulse low to reset SWIO state machine
    swio_set_input();     // release to pull-up (idle high)
    swio_high();          // ensure output value is high before enabling
    swio_set_output();
    delay(5);
    swio_low();
    delay(20);
    swio_high();
    swio_set_input();     // Release to open-drain mode
}

// ── Target power ─────────────────────────────────────────────────────────

void target_power(int on) {
    digitalWrite(TARGET_POWER_PIN, on ? HIGH : LOW);
}

// ── Arduino setup/loop ───────────────────────────────────────────────────

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

