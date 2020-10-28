#include <Arduino.h>
#include <string.h>
#include <SPI.h>
#include <SPIFFS.h>

#include "hex_file_reader.h"

#define VERBOSE

#define ATMEGA328_FLASH_SIZE  32768

// TODO: This file could use some cleanup to make the main flash_file function
// a bit more generic (such as allow any SPI port to be used).

static uint8_t atmega328pb_signature[] = {0x1e, 0x95, 0x16};

static uint8_t cmd[4]; // for SPI command buffer
static uint8_t res[4]; // for SPI response buffer
static uint8_t flash_mem[ATMEGA328_FLASH_SIZE]; // Cache of the AVR memory.


static uint8_t desired_fuses [3] = {
  0b11100010, // LFUSE, E2
  0b11010111, // HFUSE, D7
  0b11110101, // EFUSE, F5
};

SPISettings spi_settings = SPISettings(300000, SPI_MSBFIRST, SPI_MODE0);

void do_spi_transaction(void) {
  SPI.beginTransaction(spi_settings);
  SPI.transferBytes(cmd, res, 4);
  SPI.endTransaction();
}

void get_fuses(uint8_t *fuses) {
  memset(cmd, 0, 4);
  // Get lfuse
  cmd[0] = 0x50;
  do_spi_transaction();
  fuses[0] = res[3];

  // Get hfuse
  cmd[0] = 0x58;
  cmd[1] = 0x08;
  do_spi_transaction();
  fuses[1] = res[3];

  // Get efuse
  cmd[0] = 0x50;
  cmd[1] = 0x08;
  do_spi_transaction();
  fuses[2] = res[3];
}

int8_t set_fuses(uint8_t *fuse_values) {
  memset(cmd, 0, 4);
  uint8_t retries = 0;
  while(retries < 3) {
    // Set lfuse
    cmd[0] = 0xAC;
    cmd[1] = 0xA0;
    cmd[3] = fuse_values[0];
    do_spi_transaction();
    delay(6);

    // Set hfuse
    cmd[0] = 0xAC;
    cmd[1] = 0xA8;
    cmd[3] = fuse_values[1];
    do_spi_transaction();
    delay(6);

    // Set efuse
    cmd[0] = 0xAC;
    cmd[1] = 0xA4;
    cmd[3] = fuse_values[2];
    do_spi_transaction();
    delay(6);

    uint8_t check_fuses[3];
    get_fuses(check_fuses);
    if (memcmp(check_fuses, fuse_values, 3) == 0) {
      break;
    } else {
      #ifdef VERBOSE
        Serial.println("Writing fuses failed!");
      #endif
      retries += 1;
    }
  }
  if (retries >= 3) {
    #ifdef VERBOSE
      Serial.println("Error: Unable to write fuses after 3 attempts.");
    #endif 
    return -1;
  }
  else {
    return 0;
  }
}

int8_t enable_program_mode(uint8_t reset_pin) {
  #ifdef VERBOSE
    Serial.print("Enabling Programming Mode...");
  #endif
  uint8_t tries = 0;
  cmd[0] = 0xAC;
  cmd[1] = 0x53;
  do
  {
    digitalWrite(reset_pin, HIGH);
    delay(1);
    digitalWrite(reset_pin, LOW);
    delay(30);

    do_spi_transaction();
  } while(res[2] != 0x53 && tries++ < 65);

  if (tries >= 65) {
    #ifdef VERBOSE
      Serial.println("\nError: Could not enable programming mode.");
    #endif
    return -1;
  }
  #ifdef VERBOSE
    Serial.println("Done!");
  #endif
  return 0;
}

int8_t flash_file(String filename, int8_t reset_pin)
{
  int32_t    i;            /* general loop counter */
  uint32_t   size;
  uint8_t    flush_page;
  uint8_t    data;
  uint32_t   addr;
  uint8_t    start_fuses[3];
  uint8_t    end_fuses[3];
  uint8_t    signature_mem[3];

  SPI.begin();
  SPIFFS.begin();
 
  pinMode(reset_pin, OUTPUT);
  digitalWrite(reset_pin, HIGH);
  delay(1);
  digitalWrite(reset_pin, LOW);
  delay(20);
  
  //enable programming on the part
  i = enable_program_mode(reset_pin);
  if (i != 0) {
    return -1;
  }
  delay(10);

  // Read and verify signature bytes
  memset(cmd, 0, sizeof(cmd));
  cmd[0] = 0x30;
  for (i=0; i < 3; i++) {
    cmd[2] = i;
    do_spi_transaction();
    signature_mem[i] = res[3];
  }

  // Check signature against the one in the datasheet.
  if (memcmp(&signature_mem, &atmega328pb_signature, 3) != 0) {
    #ifdef VERBOSE
      Serial.print("Error: Wrong signature bytes read:");
      Serial.print(signature_mem[0], HEX);
      Serial.print(" ");
      Serial.print(signature_mem[1], HEX);
      Serial.print(" ");
      Serial.println(signature_mem[2], HEX);
    #endif
    return -1;
  }
  else {
    #ifdef VERBOSE
      Serial.println("Signature check passed - its an ATmega328PB!");
    #endif VERBOSE
  }

  // Read the fuses so we can verify they have not changed
  get_fuses(start_fuses);
  // Set the fuses if they are not what we desire
  if (memcmp(start_fuses, desired_fuses, 3) != 0) {
    i = set_fuses(desired_fuses);
    if (i != 0) {
      return -1;
    }
    memcpy(start_fuses, desired_fuses, 3);
  }
  
  // Erase all flash memory
  cmd[0] = 0xAC;
  cmd[1] = 0x80;
  do_spi_transaction();
  delay(10);

  // Load the hex file with the memory data
  File file;
  if (SPIFFS.exists(filename)) {
    file = SPIFFS.open(filename, "r");
  } else {
    #ifdef VERBOSE
      Serial.println("Requested file does not exist in SPIFFS.");
    #endif
    return -1;
  }
  

  size = ihex2b(&file, flash_mem, ATMEGA328_FLASH_SIZE, 0);
  if (size < 0) {
    #ifdef VERBOSE
      Serial.println("Error parsing firmware file.");
    #endif
    return -1;
  }

  file.close();

  // Write to flash memory
  for (addr=0; addr<size; addr++) {
    data = flash_mem[addr];
    
    if ((addr % 128) == (128 - 1) ||
        addr == (size - 1)) {
      /* last byte this page */
      flush_page = 1;
    } else {
      flush_page = 0;
    }

    // Write the data to the page buffer
    if (addr & 0x01) {
      // Load Page High
      cmd[0] = 0x48;
    }
    else {
      // Load Page Low
      cmd[0] = 0x40;
    }
    cmd[1] = 0x00;
    cmd[2] = (addr >> 1);
    cmd[3] = data;
    do_spi_transaction();

    // Flush the page if needed
    if (flush_page) {
      cmd[0] = 0x4C; // Write program memory page
      cmd[1] = (addr >> 9);
      cmd[2] = (addr >> 1);
      cmd[3] = 0x00;
      
      do_spi_transaction();

      // Wait for the write to complete.
      delay(5);
    }
  }

  // Verify the data on the ATmega328 matches what is in the buffer
  for (addr=0; addr < size; addr++) {
    if (addr & 1)
      // High Byte Read
      cmd[0] = 0x28;
    else
      // Low Byte Read
      cmd[0] = 0x20;

    cmd[1] = (addr >> 9);
    cmd[2] = (addr >> 1);
    cmd[3] = 0x00;

    do_spi_transaction();

    data = res[3];

    if (data != flash_mem[addr]) {
      #ifdef VERBOSE
        Serial.println("Error: Flash Data Verification Failed!");
        Serial.print("Flash byte ");
        Serial.print(addr, HEX);
        Serial.print(" reads ");
        Serial.print(data, HEX);
        Serial.print(" but expected ");
        Serial.println(flash_mem[addr], HEX);
      #endif
      return -1;
    }
  }
  
  // Confirm that the fuses have stayed the same and were not accidentally changed
  get_fuses(end_fuses);
  if (memcmp(start_fuses, end_fuses, 3) != 0) {
    #ifdef VERBOSE
      Serial.println("Error: Fuses have been changed unexpectedly.");
    #endif
    i = set_fuses(desired_fuses);
    if (i != 0) {
      return -1;
    }
  }

  // Let the ATmega328 exit reset state
  digitalWrite(reset_pin, HIGH);

  #ifdef VERBOSE
    Serial.println("Success uploading ATmega328 firmware!");
  #endif
  SPI.end();
}