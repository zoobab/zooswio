#line 1 "/home/zoobab/soft/zooswio/zooswio.ino"
// CH32V003 SWIO Programmer for Arduino
// Converted from AVR C implementation

#include <Arduino.h>

// Pin definitions
#define SWIO_PIN_NUM 8        // Arduino digital pin 8 (PB0 on ATmega328P)
#define TARGET_POWER_PIN 9    // Arduino digital pin 9 (PB1 on ATmega328P)

// Direct port access for timing-critical SWIO operations
#define SWIO_DDR  DDRB
#define SWIO_PORT PORTB
#define SWIO_PIN  PINB
#define SWIO_BIT  0           // PB0

// Protocol commands
#define PROTOCOL_START     '!'
#define PROTOCOL_ACK       '+'
#define PROTOCOL_TEST      '?'
#define PROTOCOL_POWER_ON  'p'
#define PROTOCOL_POWER_OFF 'P'
#define PROTOCOL_WRITE_REG 'w'
#define PROTOCOL_READ_REG  'r'

// SWIO Functions
#line 26 "/home/zoobab/soft/zooswio/zooswio.ino"
static void swio_send_one();
#line 34 "/home/zoobab/soft/zooswio/zooswio.ino"
static void swio_send_zero();
#line 47 "/home/zoobab/soft/zooswio/zooswio.ino"
static char swio_recv_bit();
#line 64 "/home/zoobab/soft/zooswio/zooswio.ino"
void swio_write_reg(uint8_t addr, uint32_t val);
#line 97 "/home/zoobab/soft/zooswio/zooswio.ino"
uint32_t swio_read_reg(uint8_t addr);
#line 131 "/home/zoobab/soft/zooswio/zooswio.ino"
void swio_init();
#line 141 "/home/zoobab/soft/zooswio/zooswio.ino"
void target_power(int x);
#line 145 "/home/zoobab/soft/zooswio/zooswio.ino"
void setup();
#line 160 "/home/zoobab/soft/zooswio/zooswio.ino"
void loop();
#line 26 "/home/zoobab/soft/zooswio/zooswio.ino"
static inline void swio_send_one() {
    /* 0.0T */ SWIO_DDR |= _BV(SWIO_BIT);
    /* 1.0T */ SWIO_PORT &= ~_BV(SWIO_BIT);
    /* 2.0T */ asm("nop"); asm("nop");
    /* 3.0T */ SWIO_PORT |= _BV(SWIO_BIT);
    /* 4.0T */ SWIO_DDR &= ~_BV(SWIO_BIT);
}

static inline void swio_send_zero() {
    /* 0.0T */ SWIO_DDR |= _BV(SWIO_BIT);
    /* 1.0T */ SWIO_PORT &= ~_BV(SWIO_BIT);
    /* 2.0T */ asm("nop"); asm("nop");
    /* 3.0T */ asm("nop"); asm("nop");
    /* 4.0T */ asm("nop"); asm("nop");
    /* 5.0T */ asm("nop"); asm("nop");
    /* 6.0T */ asm("nop"); asm("nop");
    /* 7.0T */ asm("nop"); asm("nop");
    /* 8.0T */ SWIO_PORT |= _BV(SWIO_BIT);
    /* 9.0T */ SWIO_DDR &= ~_BV(SWIO_BIT);
}

static inline char swio_recv_bit() {
    char x;
    /* 0.0T */ SWIO_DDR |= _BV(SWIO_BIT);
    /* 1.0T */ SWIO_PORT &= ~_BV(SWIO_BIT);
    /* 2.0T */ SWIO_PORT |= _BV(SWIO_BIT); // Precharge when the line is floating.
    /* 3.0T */ SWIO_DDR &= ~_BV(SWIO_BIT);
    /* 4.0T */ asm("nop"); asm("nop");
    /* 5.0T */ asm("nop"); asm("nop");
    /* 6.0T */ x = SWIO_PIN;

    // Wait for the line to come back up if it's down.
    while (!(SWIO_PIN & _BV(SWIO_BIT)))
        ;

    return x & _BV(SWIO_BIT);
}

void swio_write_reg(uint8_t addr, uint32_t val) {
    char i;

    // Start bit.
    swio_send_one();

    // Send the address.
    for (i = 0; i < 7; i++) {
        if (addr & 0x40)
            swio_send_one();
        else
            swio_send_zero();

        addr <<= 1;
    }

    // Start bit.
    swio_send_one();

    // Send the word.
    for (i = 0; i < 32; i++) {
        if (val & 0x80000000)
            swio_send_one();
        else
            swio_send_zero();

        val <<= 1;
    }

    // Stop bit.
    delayMicroseconds(10);
}

uint32_t swio_read_reg(uint8_t addr) {
    char i;
    uint32_t x = 0;

    // Start bit.
    swio_send_one();

    // Send the address.
    for (i = 0; i < 7; i++) {
        if (addr & 0x40)
            swio_send_one();
        else
            swio_send_zero();

        addr <<= 1;
    }

    // Start bit.
    swio_send_zero();

    // Receive the word.
    for (i = 0; i < 32; i++) {
        x <<= 1;

        if (swio_recv_bit())
            x |= 1;
    }

    // Stop bit.
    delayMicroseconds(10);

    return x;
}

void swio_init() {
    SWIO_PORT |= _BV(SWIO_BIT);
    SWIO_DDR |= _BV(SWIO_BIT);
    delay(5);
    SWIO_PORT &= ~_BV(SWIO_BIT);
    delay(20);
    SWIO_PORT |= _BV(SWIO_BIT);
    SWIO_DDR &= ~_BV(SWIO_BIT);
}

void target_power(int x) {
    digitalWrite(TARGET_POWER_PIN, x ? HIGH : LOW);
}

void setup() {
    // Initialize serial communication
    Serial.begin(115200);
    
    // Configure target power pin
    pinMode(TARGET_POWER_PIN, OUTPUT);
    digitalWrite(TARGET_POWER_PIN, LOW);
    
    // Initialize SWIO
    swio_init();
    
    // Send start character
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
                // Wait for register address
                while (Serial.available() < 1);
                Serial.readBytes((char*)&reg, sizeof(uint8_t));
                
                // Wait for value
                while (Serial.available() < 4);
                Serial.readBytes((char*)&val, sizeof(uint32_t));
                
                swio_write_reg(reg, val);
                Serial.write(PROTOCOL_ACK);
                break;
                
            case PROTOCOL_READ_REG:
                // Wait for register address
                while (Serial.available() < 1);
                Serial.readBytes((char*)&reg, sizeof(uint8_t));
                
                val = swio_read_reg(reg);
                Serial.write((uint8_t*)&val, sizeof(uint32_t));
                break;
        }
    }
}

