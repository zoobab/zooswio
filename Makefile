# Arduino CLI Makefile
# Ensure you have arduino-cli installed and configured

# Specify the Arduino CLI executable path
ARDUINO_CLI = arduino-cli

# Set the Arduino board type and port
BOARD = esp8266:esp8266:d1_mini  # ESP8266 board (Wemos D1 mini)
#BOARD = arduino:avr:uno         # AVR alternative
PORT = /dev/ttyUSB0              # Change this to your connected port

# Specify your sketch name
SKETCH = zooswio.ino

# Build directory
BUILD_DIR = build

# Default target
all: build upload

# Build target
build: $(SKETCH)
	@echo "=================================================================="
	@echo "Compiling $(SKETCH) for $(BOARD) ..."
	@echo "=================================================================="
	$(ARDUINO_CLI) compile --fqbn $(BOARD) --build-path $(BUILD_DIR) $(SKETCH)

# Upload target
upload:
	@echo "=================================================================="
	@echo "Flashing $(SKETCH) ..."
	@echo "=================================================================="
	$(ARDUINO_CLI) upload -v -p $(PORT) --fqbn $(BOARD) --input-dir $(BUILD_DIR) $(SKETCH)
	@echo "=================================================================="
	@echo "The end!"
	@echo "=================================================================="

# Clean target
clean:
	rm -rf $(BUILD_DIR)

.PHONY: all build upload clean
