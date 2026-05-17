#line 1 "/home/zoobab/soft/zooswio/zooswio.ino"
// CH32V003 SWIO Programmer for CH32V003
// Ported from RP2040 (zoobab/zooswio) to native CH32V003
//
// Key differences from RP2040 version:
//   - RP2040 ARM Cortex-M0+ SIO registers replaced with CH32V003 RISC-V GPIO registers
//   - NOP timing recalibrated: RP2040 @125MHz ~8ns/nop, CH32V003 @48MHz ~20.83ns/nop
//     -> each "T" slot (~250ns) now requires ~12 NOPs instead of ~32
//   - Direct RISC-V CSR-based NOP loops for timing-critical bit-banging
//   - GPIO register access via GPIOD_BSHR/GPIOx_BCR for atomic bit set/reset
//   - Pin numbers changed to CH32V003 GPIO numbers (3.3V logic)
//
// This version runs on a CH32V003 and can program another CH32V003 via SWIO.
// It's a self-hosting programmer — a CH32V003 flashing another CH32V003!
//
// Wiring:
//   PD0 (pin 17) -> SWIO (single-wire debug I/O to target CH32V003)
//   PD1 (pin 13) -> TARGET_POWER (optional, controls target VCC via transistor/MOSFET)
//
// Serial is on the default USART1: PD5 (TX) / PD6 (RX) at 115200 baud.
// Board: CH32V003F4P6 (TSSOP-20) or any CH32V003 with OpenWCH Arduino core
// Flash
// with: arduino-cli compile --fqbn ch32v003:ch32v003:ch32v003f4p6 zooswio
//             arduino-cli upload  --fqbn ch32v003:ch32v003:ch32v003f4p6 -p /dev/ttyUSB0 zooswio

#include <Arduino.h>

// ── Pin definitions ──────────────────────────────────────────────────────────
// Using GPIOD pins (available on CH32V003F4P6 TSSOP-20)
#define SWIO_PORT       GPIOD
#define SWIO_PIN_NUM    0       // PD0 - SWIO data line (pin 17)
#define SWIO_MASK       (1 << SWIO_PIN_NUM)

#define TARGET_POWER_PIN   1       // PD1 - target power control (pin 13)

// ── Direct GPIO register access for timing-critical operations ──────────────
// CH32V003 GPIO registers (GD32V GPIO) per RM:
//   OUTDR (offset 0x0C) - Output data register
//   BSHR  (offset 0x10) - Bit set/reset: bits 0-15 set, bits 16-31 reset
//   BCR   (offset 0x14) - Bit reset register
//   INDR  (offset 0x08) - Input data register

#define GPIO_OUTDR(base)  (*(volatile uint32_t *)((base) + 0x0C))
#define GPIO_BSHR(base)   (*(volatile uint32_t *)((base) + 0x10))
#define GPIO_BCR(base)    (*(volatile uint32_t *)((base) + 0x14))
#define GPIO_INDR(base)   (*(volatile uint32_t *)((base) + 0x08))

// GPIOD base address (per CH32V003 memory map)
#define GPIOD_BASE        0x40011400UL

// Fast GPIO macros for SWIO pin on GPIOD
// SWIO_MASK = (1 << 0) = 0x0001 for PD0
#define swio_set_output()    do { \
    GPIO_BSHR(GPIOD_BASE) = SWIO_MASK; \
} while(0)

#define swio_set_input()     do { \
    GPIO_BCR(GPIOD_BASE)  = SWIO_MASK; \
} while(0)

#define swio_high()          do { \
    GPIO_BSHR(GPIOD_BASE) = SWIO_MASK; \
} while(0)

#define swio_low()           do { \
    GPIO_BCR(GPIOD_BASE)  = SWIO_MASK; \
} while(0)

#define swio_read()          ((GPIO_INDR(GPIOD_BASE) & SWIO_MASK) ? 1 : 0)

// ── Protocol commands (unchanged from original) ──────────────────────────────
#define PROTOCOL_START     '!'
#define PROTOCOL_ACK       '+'
#define PROTOCOL_TEST      '?'
#define PROTOCOL_POWER_ON  'p'
#define PROTOCOL_POWER_OFF 'P'
#define PROTOCOL_WRITE_REG 'w'
#define PROTOCOL_READ_REG  'r'

// ── Timing helpers ───────────────────────────────────────────────────────────
// CH32V003 default CPU: 48 MHz (HSI)  -> 1 cycle = ~20.83 ns
// One SWIO "T" slot ≈ 250 ns -> ~12 CPU cycles.
// We use inline assembly NOPs to burn precise cycles.
//
// GPIO register writes (BSHR/BCR) take ~2-3 cycles via the peripheral bus,
// so NOP counts are adjusted to keep the total between-state time near 250 ns.
//
// At 48 MHz: 12 nops ≈ 250 ns  ✓  (~1T)
// At  24 MHz:  6 nops ≈ 250 ns

#define NOP1  __asm__ __volatile__("nop")
#define NOP4  NOP1; NOP1; NOP1; NOP1
#define NOP8  NOP4; NOP4
#define NOP12 NOP8; NOP4

// One full-T pause (~250ns at 48MHz)
#define ONE_T  NOP12

// ── SWIO bit primitives ──────────────────────────────────────────────────────
//
// Bit encoding (same ardulink protocol):
//   ONE  : drive LOW for ~1T, then HIGH for ~3T (short low pulse)
//   ZERO : drive LOW for ~7T, then HIGH for ~2T (long low pulse)
//   RECV : drive LOW for ~1T, precharge HIGH, release, sample at ~5T

#line 105 "/home/zoobab/soft/zooswio/zooswio.ino"
static void swio_send_one();
#line 114 "/home/zoobab/soft/zooswio/zooswio.ino"
static void swio_send_zero();
#line 127 "/home/zoobab/soft/zooswio/zooswio.ino"
static char swio_recv_bit();
#line 146 "/home/zoobab/soft/zooswio/zooswio.ino"
void swio_write_reg(uint8_t addr, uint32_t val);
#line 175 "/home/zoobab/soft/zooswio/zooswio.ino"
uint32_t swio_read_reg(uint8_t addr);
#line 207 "/home/zoobab/soft/zooswio/zooswio.ino"
void swio_init();
#line 229 "/home/zoobab/soft/zooswio/zooswio.ino"
void target_power(int on);
#line 235 "/home/zoobab/soft/zooswio/zooswio.ino"
void setup();
#line 252 "/home/zoobab/soft/zooswio/zooswio.ino"
void loop();
#line 105 "/home/zoobab/soft/zooswio/zooswio.ino"
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
    ONE_T;                // T1-T7: hold low ~7T (long pulse = '0')
    int i;
    for (i = 0; i < 7; i++) {
        ONE_T;
    }
    swio_high();          // T7->T8: go high
    ONE_T;                // brief high
    swio_set_input();     // release
}

static inline char swio_recv_bit() {
    char x;
    swio_set_output();
    swio_low();           // T0: drive low (initiate read)
    ONE_T;                // wait
    swio_high();          // precharge line before releasing
    swio_set_input();     // release - target drives line
    ONE_T;                // wait for target to drive its response
    x = swio_read();      // sample

    // Wait for the line to come back up if it's down
    while (!swio_read())
        ;

    return x;
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

// ── SWIO init ────────────────────────────────────────────────────────────────

void swio_init() {
    // Configure PD0 as push-pull output, initially high
    pinMode(SWIO_PIN_NUM, OUTPUT);
    digitalWrite(SWIO_PIN_NUM, HIGH);

    // Set PD0 to open-drain mode via GPIO config register
    // CFGLR: 4 bits per pin, mode=0b0111 (GPIO_Mode_Out_OD), cnf=0b0111
    // Actually for open-drain: MODE=0b11 (50MHz), CNF=0b01 (Open-Drain)
    // But the Arduino core handles this via pinMode.
    // For fast switching we rely on BSHR/BCR for atomic bit control.

    // Pulse low to reset SWIO state machine on target
    delay(5);
    swio_set_output();
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
    // Initialize serial at 115200 baud (USART1 on PD5/PD6)
    Serial.begin(115200);
    while (!Serial)
        ;  // Wait for serial (useful when USB-Serial adapter is used)

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
