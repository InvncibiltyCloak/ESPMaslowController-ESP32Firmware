#ifndef __ATMEGA328_FLASHER_H__
#define __ATMEGA328_FLASHER_H__

#include <Arduino.h>

// Loads a hex file from SPIFFS with the given filename and flashes it to
// an ATmega328PB via the SPI port.

int8_t flash_file(String filename, int8_t reset_pin);


#endif //__ATMEGA328_FLASHER_H__