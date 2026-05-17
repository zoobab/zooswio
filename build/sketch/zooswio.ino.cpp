#line 1 "/home/zoobab/soft/zooswio/zooswio.ino"
// CH32V003 SWIO Programmer for ESP8266
// Ported from ESP32 (zoobab/zooswio) to ESP8266
//
// Key differences from ESP32 version:
//   - ESP32 IDF GPIO API (gpio_set_level/gpio_get_level) replaced with ESP8266 direct
//     register manipulation (GPOS/GPOC/GPES/GPEC/GPIP)
//   - NOP timing recalibrated: ESP32 @240MHz ~4ns/nop, ESP8266 @80MHz ~12.5ns/nop
//     -> each "T" slot (~250ns) now requires ~20 NOPs instead of ~60
//   - ESP8266 has no dedicated pull-up control per-pin; we use pinMode(INPUT_PULLUP) instead
//   - No IRAM_ATTR needed (ESP8266 core handles cache differently)
//   - Pin numbers use ESP8266 GPIO numbering (3.3V logic, matches CH32V003)
//
// Wiring (same protocol as BlueSyncLine):
//   GPIO4 (D2) -> SWIO (single-wire debug I/O to CH32V003)
//   GPIO5 (D1) -> TARGET_POWER (optional, controls target VCC via transistor/MOSFET)
//
// Board: ESP8266 (Wemos D1 mini, NodeMCU, etc.)
// Flash with: arduino-cli compile --fqbn esp8266:esp8266:d1_mini zooswio.ino
//             arduino-cli upload  --fqbn esp8266:esp8266:d1_mini -p /dev/ttyUSB0 zooswio.ino

#include <Arduino.h>

// ── Pin definitions ──────────────────────────────────────────────────────────
#define SWIO_PIN_NUM      4   // GPIO4 (D2) - SWIO data line
#define TARGET_POWER_PIN  5   // GPIO5 (D1) - target power control
#define SWIO_BIT          (1 << SWIO_PIN_NUM)

// ── Protocol commands (unchanged from original) ──────────────────────────────
#define PROTOCOL_START     '!'
#define PROTOCOL_ACK       '+'
#define PROTOCOL_TEST      '?'
#define PROTOCOL_POWER_ON  'p'
#define PROTOCOL_POWER_OFF 'P'
#define PROTOCOL_WRITE_REG 'w'
#define PROTOCOL_READ_REG  'r'

// ── Timing helpers ───────────────────────────────────────────────────────────
// ESP8266 default CPU: 80 MHz -> 1 cycle = ~12.5 ns
// One SWIO "T" slot ≈ 250 ns -> ~20 CPU cycles.
// We use NOP macros to burn cycles. Adjust NOP_PER_T if your ESP8266 runs at
// 160 MHz (overclocked).
//
// At 80 MHz: 20 nops ≈ 250 ns  ✓
// At 160 MHz: 40 nops ≈ 250 ns  (if overclocked, change NOP macros)

#define NOP1  __asm__ __volatile__("nop")
#define NOP4  NOP1; NOP1; NOP1; NOP1
#define NOP8  NOP4; NOP4
#define NOP16 NOP8; NOP8
#define NOP20 NOP16; NOP4

// One T slot (~250 ns at 80 MHz)
#define ONE_T  NOP20

// ── Fast GPIO inlines ────────────────────────────────────────────────────────
// Using ESP8266 direct register manipulation for speed.
// Registers (defined in the ESP8266 Arduino core):
//   GPOS = GPIO_OUT_W1TS  (write 1 to set output high)
//   GPOC = GPIO_OUT_W1TC  (write 1 to set output low)
//   GPES = GPIO_ENABLE_W1TS (write 1 to enable output)
//   GPEC = GPIO_ENABLE_W1TC (write 1 to disable output)
//   GPIP = GPIO_IN        (read input levels)

#line 64 "/home/zoobab/soft/zooswio/zooswio.ino"
static void swio_set_output();
#line 68 "/home/zoobab/soft/zooswio/zooswio.ino"
static void swio_set_input();
#line 73 "/home/zoobab/soft/zooswio/zooswio.ino"
static void swio_high();
#line 77 "/home/zoobab/soft/zooswio/zooswio.ino"
static void swio_low();
#line 81 "/home/zoobab/soft/zooswio/zooswio.ino"
static int swio_read();
#line 92 "/home/zoobab/soft/zooswio/zooswio.ino"
static void swio_send_one();
#line 101 "/home/zoobab/soft/zooswio/zooswio.ino"
static void swio_send_zero();
#line 116 "/home/zoobab/soft/zooswio/zooswio.ino"
static char swio_recv_bit();
#line 135 "/home/zoobab/soft/zooswio/zooswio.ino"
void swio_write_reg(uint8_t addr, uint32_t val);
#line 164 "/home/zoobab/soft/zooswio/zooswio.ino"
uint32_t swio_read_reg(uint8_t addr);
#line 194 "/home/zoobab/soft/zooswio/zooswio.ino"
void swio_init();
#line 211 "/home/zoobab/soft/zooswio/zooswio.ino"
void target_power(int on);
#line 217 "/home/zoobab/soft/zooswio/zooswio.ino"
void setup();
#line 231 "/home/zoobab/soft/zooswio/zooswio.ino"
void loop();
#line 64 "/home/zoobab/soft/zooswio/zooswio.ino"
static inline void swio_set_output() {
    GPES = SWIO_BIT;
}

static inline void swio_set_input() {
    GPEC = SWIO_BIT;
    // Pull-up was set once during init via pinMode(INPUT_PULLUP)
}

static inline void swio_high() {
    GPOS = SWIO_BIT;
}

static inline void swio_low() {
    GPOC = SWIO_BIT;
}

static inline int swio_read() {
    return (GPI & SWIO_BIT) ? 1 : 0;
}

// ── SWIO bit primitives ──────────────────────────────────────────────────────
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
    ONE_T;                // T1: hold low
    swio_high();          // T2: precharge line before releasing
    swio_set_input();     // T3: release - target drives line
    ONE_T;                // T4-T5: wait for target to drive its response
    x = swio_read();      // T6: sample

    // Wait for line to return high (open-drain release by target)
    while (!swio_read())
        ;

    return (char)x;
}

// ── Register-level SWIO transactions ────────────────────────────────────────

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
    // Configure SWIO pin with pull-up
    pinMode(SWIO_PIN_NUM, INPUT_PULLUP);

    // Pull line high (idle), then pulse low to reset SWIO state machine
    swio_set_input();     // release to pull-up (idle high)
    swio_set_output();    // enable output
    swio_high();          // ensure output is high
    delay(5);
    swio_low();
    delay(20);
    swio_high();
    swio_set_input();     // Release to open-drain mode
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

