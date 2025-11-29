About
=====

(WARNING WIP) A re-implementation of a BlueSyncLine's ch32v003 arduino flasher in pure
Arduino INO, with less assembler specific to a particular platform.

Status
======

It is detecting now the chip, but sometimes unstable:

```
+ ../../ch32fun//../minichlink/minichlink -C ardulink -c /dev/ttyACM0 -i
Opening serial port /dev/ttyACM0 at 115200 baud.
Ardulink: synced.
Ardulink: target power 1
Interface Setup
Detected CH32V003
Flash Storage: 16 kB
Part UUID: 08-d7-ab-cd-71-0d-bc-69
Part Type: ff-ff-ff-ff
Read protection: disabled
USER/RDPR  : 0005/0000
DATA1/DATA0: e817/5aa5
WRPR1/WRPR0: 00ff/00ff
WRPR3/WRPR2: 00ff/00ff
R32_ESIG_UNIID1: 08d7abcd
R32_ESIG_UNIID2: 710dbc69
R32_ESIG_UNIID3: 00050000
```

Tested microcontrollers
=======================

Arduino UNO (328p)

Wiring
======

Same wiring as the BlueSyncLine.

Edit the first 2 lines to change the pin if if does not work for your board.

Todo
====

0. Document how to install arduino-cli
1. Document a little bit more the protocol (250ns blocks)
2. example register to validate we can communicate with the ch32v003 (which one?)
3. Document Arduino cli compilation and flashing (no IDE)
4. Problem with 3.3v ch32v003 boards (power from somewhere else)
5. Optional power pin?
6. Add a fritzing style diagram, including PC with minichlink
7. Test blink with Arduino port for ch32v003

Links
=====

* https://gitlab.com/BlueSyncLine/arduino-ch32v003-swio
