# ESPMaslowController-ESP32Firmware
The firmware repo for the firmware used on the ESPMaslowController. It provides a way to test all 5 motor channels.

This software first flashes the ATmega328 firmware from a file in the SPIFFS (SPI Flash File System) on the ESP32.

Then it sends commands to the newly-programmed ATmega328 to send a PWM to the motor drivers to spin the motors. 

The motors spin for one rotation in one direction, then reverse direction until they reach their starting position, where they are stopped.

## atmega328_flasher library
This is included in the lib/atmega328_flasher subfolder. It contains a single function `flash_file` which flashes a `.hex` file to the ATmega328.

The program also sets the fuses on the ATmega to run at 8 MHz and a few other settings preferred for this project. 

In the `atmega328_flasher.cpp` source file you can undefine `VERBOSE` to suppress output on the serial port.

