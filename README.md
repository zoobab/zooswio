About
=====

(WARNING WIP) A re-implementation of a BlueSyncLine's ch32v003 arduino flasher in pure
Arduino INO, with less assembler specific assembler for a particular platform.

This is the **ESP8266** branch. Defaults to Wemos D1 mini / NodeMCU boards.

Status
======

It is detecting the chip, but sometimes unstable (see this issue https://github.com/cnlohr/ch32fun/issues/629).

Wiring
======

Same wiring as the BlueSyncLine protocol BlueSyncLine.

| ESP8266 GPIO | Pin | Function       |
|-------------|-----|----------------|
| GPIO4       | D2  | SWIO           |
| GPIO5       | D1  | TARGET_POWER   |

Edit the first 2 lines to change the pin if it does not work for your board.

Tested microcontrollers
=======================

Wemos D1 mini (ESP8266)

Install the arduino-cli support for ESP8266
============================================

```
arduino-cli config set board_manager.additional_urls https://arduino.esp8266.com/stable/package_esp8266com_index.json
arduino-cli core update-index
arduino-cli core install esp8266:esp8266
```

Compile and upload
==================

```
arduino-cli compile --fqbn esp8266:esp8266:d1_mini zooswio.ino
arduino-cli upload --fqbn esp8266:esp8266:d1_mini -p /dev/ttyUSB0 zooswio.ino
```

Or just use `make build` and `make upload`.

Todo
====

0. protocol
1. Register we can validate we can communicate with the ch32v003 (which one?)
2. power off after flashing?
3. Reset?
4. Problem with 3.3v ch32v003 boards (power from elsewhere)
5. Example with minichlink

Links
=====

* https://gitlab.com/BlueSyncLine/arduino-ch32v003-swio