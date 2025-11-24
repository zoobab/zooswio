# Arduino CLI Makefile
# Ensure you have arduino-cli installed and configured

# Specify the Arduino CLI executable path
ARDUINO_CLI = arduino-cli

# Set the Arduino board type and port
BOARD = arduino:avr:uno  # Change this to your Arduino board type
PORT = /dev/ttyUSB0      # Change this to your connected port

# Specify your sketch name
SKETCH = zooswio.ino

# Build directory
BUILD_DIR = build

# Library to check and install
LIBRARY = digitalWriteFast

# Default target
all: libinstall build upload

# Check if the library is installed
libinstall:
	@echo "==== Installing library $(LIBRARY) ... ===="
	$(ARDUINO_CLI) lib install $(LIBRARY)
#	@if ! $(ARDUINO_CLI) lib is-installed $(LIBRARY); then \
		echo "Installing $(LIBRARY) library..."; \
		$(ARDUINO_CLI) lib install $(LIBRARY); \
	fi

# Build target
build: $(SKETCH)
	@echo "==== Compiling $(SKETCH) ... ===="
	$(ARDUINO_CLI) compile --fqbn $(BOARD) --build-path $(BUILD_DIR) $(SKETCH)

# Upload target
upload:
	@echo "==== Flashing $(SKETCH) ... ===="
	$(ARDUINO_CLI) upload -p $(PORT) --fqbn $(BOARD) $(SKETCH)

# Clean target
clean:
	rm -rf $(BUILD_DIR)

.PHONY: all libinstall build upload clean
