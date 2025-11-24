// Documentation about bits and timings, see here https://github.com/aappleby/picorvd/blob/master/src/singlewire.pio#L28C1-L31C23
//
// We should be able to craft bits with the following timings:
// 
// Normal mode:
// 1    = low 125ns to  500ns, high 125ns to 2000ns
// 0    = low 750ns to 8000ns, high 125ns to 2000ns
// Stop = high 2250 ns

const int SWIO_PIN = 8; // Edit your SWIO pin (depending on your board)

// for AVR only for now you need to install the arduino lib digitalWriteFast
#include <digitalWriteFast.h>

void delayNanoseconds(uint16_t ns) {
    // Convert nanoseconds to clock cycles
    // F_CPU is the clock frequency in Hz, so divide by 1 billion (1e9) to get cycles
    uint16_t nops = (ns * F_CPU) / 1000000000UL;

    // Use 'nop' instructions to create the delay
    while (nops--) {
        asm volatile("nop");
    }
}

void swio_send_one() {
    pinModeFast(SWIO_PIN, OUTPUT);
    digitalWriteFast(SWIO_PIN, LOW);
    delayNanoseconds(500);
    digitalWriteFast(SWIO_PIN, HIGH);
    pinModeFast(SWIO_PIN, INPUT);
}

void swio_send_zero() {
    pinModeFast(SWIO_PIN, OUTPUT);
    digitalWriteFast(SWIO_PIN, LOW);
    delayNanoseconds(750);
    digitalWriteFast(SWIO_PIN, HIGH);
    pinModeFast(SWIO_PIN, INPUT);
}

char swio_recv_bit() {
    char x;
    pinModeFast(SWIO_PIN, OUTPUT);
    digitalWriteFast(SWIO_PIN, LOW);
    digitalWriteFast(SWIO_PIN, HIGH);
    pinModeFast(SWIO_PIN, INPUT);
    delayNanoseconds(500);
    x = digitalReadFast(SWIO_PIN);
    // Wait for the line to come back up if it's down.
    while (digitalReadFast(SWIO_PIN) == LOW);
        return x;
}

// Write a register.
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
    delayNanoseconds(2250);
}

// Read a register.
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
    delayNanoseconds(2250);

    return x;
}

void swio_init() {
    pinModeFast(SWIO_PIN, OUTPUT);
    digitalWriteFast(SWIO_PIN, HIGH);
    delay(5);
    digitalWriteFast(SWIO_PIN, LOW);
    delay(20);
    digitalWriteFast(SWIO_PIN, HIGH);
    pinModeFast(SWIO_PIN, INPUT);
}

void setup() {
    #define PROTOCOL_START     '!'
    #define PROTOCOL_ACK       '+'
    #define PROTOCOL_TEST      '?'
    #define PROTOCOL_POWER_ON  'p'
    #define PROTOCOL_POWER_OFF 'P'
    #define PROTOCOL_WRITE_REG 'w'
    #define PROTOCOL_READ_REG  'r'
    Serial.begin(115200);
    swio_init();
    Serial.println(PROTOCOL_START);
}

void loop() {
    uint8_t reg;
    uint32_t val;
    while (Serial.available() >= 0) {
    char receivedData = Serial.read();
    if (receivedData == PROTOCOL_TEST ) {
      Serial.println(PROTOCOL_ACK);
      break;
    }
    else if (receivedData == PROTOCOL_WRITE_REG ) {
      if (Serial.available() >= sizeof(reg) + sizeof(val)) {
        reg = Serial.read();
        val = 0;
        for (int i = 0; i < sizeof(val); i++) {
          val |= ((uint32_t)Serial.read()) << (8 * i);
        }
        swio_write_reg(reg, val);
        Serial.print(PROTOCOL_ACK);
        break;
      }
    }
    else if (receivedData == PROTOCOL_READ_REG ) {
      if (Serial.available() >= sizeof(reg)) {
        reg = Serial.read();
        val = swio_read_reg(reg);
        Serial.write((uint8_t*)&val, sizeof(val));
      }
      break;
      }
    }
} 
