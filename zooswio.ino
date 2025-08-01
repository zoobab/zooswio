const int SWIO_PIN = 8; // Edit your SWIO pin
const int TARGET_POWER_PIN = 9; // Edit your POWER pin

void target_power(int x) {
    if (x)
        digitalWrite(TARGET_POWER_PIN, HIGH);
    else
        digitalWrite(TARGET_POWER_PIN, LOW);
}

void swio_send_one() {
    // 0.0T
    pinMode(SWIO_PIN, OUTPUT);
    digitalWrite(SWIO_PIN, LOW);
    
    // 1.0T
    delayMicroseconds(1);
    
    // 2.0T
    digitalWrite(SWIO_PIN, HIGH);
    
    // 3.0T
    pinMode(SWIO_PIN, INPUT);
}

void swio_send_zero() {
    // 0.0T
    pinMode(SWIO_PIN, OUTPUT);
    digitalWrite(SWIO_PIN, LOW);
    
    // 1.0T
    delayMicroseconds(1);
    
    // 2.0T to 7.0T (8 nops)
    delayMicroseconds(8);
    
    // 8.0T
    digitalWrite(SWIO_PIN, HIGH);
    
    // 9.0T
    pinMode(SWIO_PIN, INPUT);
}

char swio_recv_bit() {
    char x;
    // 0.0T
    pinMode(SWIO_PIN, OUTPUT);
    digitalWrite(SWIO_PIN, LOW);
    
    // 1.0T
    digitalWrite(SWIO_PIN, HIGH);
    
    // 2.0T
    pinMode(SWIO_PIN, INPUT);
    
    // 3.0T to 4.0T (2 nops)
    delayMicroseconds(2);
    
    // 5.0T
    x = digitalRead(SWIO_PIN);

    // Wait for the line to come back up if it's down.
    while (digitalRead(SWIO_PIN) == LOW)
        ;

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
    delayMicroseconds(10);
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
    delayMicroseconds(10);

    return x;
}

void swio_init() {
    pinMode(SWIO_PIN, OUTPUT);
    digitalWrite(SWIO_PIN, HIGH);
    delay(5);
    digitalWrite(SWIO_PIN, LOW);
    delay(20);
    digitalWrite(SWIO_PIN, HIGH);
    pinMode(SWIO_PIN, INPUT);
}

void power() {
    pinMode(SWIO_PIN, OUTPUT);
    digitalWrite(SWIO_PIN, HIGH);
    delay(5);
    digitalWrite(SWIO_PIN, LOW);
    delay(20);
    digitalWrite(SWIO_PIN, HIGH);
    pinMode(SWIO_PIN, INPUT);
}

void setup() {
    Serial.begin(115200);
    #define PROTOCOL_START     '!'
    #define PROTOCOL_ACK       '+'
    #define PROTOCOL_TEST      '?'
    #define PROTOCOL_POWER_ON  'p'
    #define PROTOCOL_POWER_OFF 'P'
    #define PROTOCOL_WRITE_REG 'w'
    #define PROTOCOL_READ_REG  'r'
    swio_init();
    Serial.println("SWIO flasher ready, waiting for commands...");
}

void loop() {
    uint8_t reg;
    uint32_t val;
    
    while (Serial.available() >= 0) {
    char receivedData = Serial.read();   // read one byte from serial buffer and save to receivedData
    if (receivedData == PROTOCOL_TEST ) {
      Serial.println(PROTOCOL_ACK);
      break;
    }
    else if (receivedData == PROTOCOL_POWER_ON ) {
      target_power(1);
      Serial.println(PROTOCOL_ACK);
      break;
    }
    else if (receivedData == PROTOCOL_POWER_OFF ) {
      target_power(0);
      Serial.println(PROTOCOL_ACK);
      break;
    }
    else if (receivedData == PROTOCOL_WRITE_REG ) {
      //  reg = Serial.read();
      //  val = Serial.read() << 24;
      //  val |= Serial.read() << 16;
      //  val |= Serial.read() << 8;
      //  val |= Serial.read();
        swio_write_reg(0x01, 0x12345678);
      //  swio_write_reg(reg, val);
        Serial.print(PROTOCOL_ACK);
    }
    
  }
  // Example usage:
  //swio_write_reg(0x01, 0x12345678);
  //uint32_t value = swio_read_reg(0x01);
  //Serial.println(value, HEX);
}
