About
=====

A re-implementation of a BlueSyncLine's ch32v003 arduino flasher in pure
Arduino INO, without any AVR assembler specific to a particular platform.

Tested microcontrollers
=======================

Put a list of tested UCs here.

Wiring
======

Same wiring as the BlueSyncLine.

Edit the first 2 lines to change the pin if if does not work for your board.

Todo
====

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
