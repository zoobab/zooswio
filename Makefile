# Arduino CLI Makefile
# Ensure you have arduino-cli installed and configured
# RP2040 board support: https://github.com/earlephilhower/arduino-pico
#   arduino-cli core install rp2040:rp2040

# Specify the Arduino CLI executable path
ARDUINO_CLI = arduino-cli

# Set the Arduino board type and port
BOARD = rp2040:rp2040:generic
#BOARD = rp2040:rp2040:rp2040    # generic RP2040
#BOARD = rp2040:rp2040:adafruit-feather-rp2040
PORT = /dev/ttyACM0            # Change this to your connected port

# Specify your sketch name
SKETCH = zooswio.ino

# Build directory
BUILD_DIR = build

# Default target
all: build upload

# Build target
build: $(SKETCH)
	@echo "=================================================================="
	@echo "Compiling $(SKETCH) ..."
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
