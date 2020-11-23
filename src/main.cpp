#include <Arduino.h>
#include "pindefs.h"
#include <ESP32Encoder.h>
#include <SPI.h>

#include "atmega328_flasher.h"

#define ENCODER_TARGET_COUNT 8113

ESP32Encoder encoders[5];

static SPISettings spi_settings = SPISettings(1000, SPI_MSBFIRST, SPI_MODE0);

void controller_write(uint8_t address, uint8_t value1, uint8_t value2) {
    uint8_t spi_message[4] = {address, value1, value2, 0x00};
    SPI.beginTransaction(spi_settings);
    delay(1);
    SPI.transferBytes(spi_message, spi_message, 4);
    Serial.print("Response is ");
    Serial.print(spi_message[0]);
    Serial.print(", ");
    Serial.print(spi_message[1]);
    Serial.print(", ");
    Serial.println(spi_message[2]);
    delay(1);
    SPI.endTransaction();
    delay(1);
}

void setup(void) {
    Serial.begin(115200);
    delay(1000); // Wait for serial monitor

    Serial.println("Flashing ATmega328 firmware");
    flash_file("/firmware.hex", AVR_RESET_PIN);
    delay(100);
    
    Serial.println("Setting up encoders");
    ESP32Encoder::useInternalWeakPullResistors = NONE;
    encoders[0].attachFullQuad(ENC1A_PIN, ENC1B_PIN);
    encoders[1].attachFullQuad(ENC2A_PIN, ENC2B_PIN);
    encoders[2].attachFullQuad(ENC3A_PIN, ENC3B_PIN);
    encoders[3].attachFullQuad(ENC4A_PIN, ENC4B_PIN);
    encoders[4].attachFullQuad(ENC5A_PIN, ENC5B_PIN);

    SPI.begin();
    SPI.setHwCs(true);
    Serial.println("Spinning motors");

    // Set all motors to low power
    for(uint8_t motor_num = 0; motor_num < 5; motor_num++) {
        controller_write(motor_num+1, 0x30, 0x00);
    }
    Serial.println("Waiting for motors...");
}

// First spin motor one turn in positive direction
// Second spin motor one turn in negative direction
// Third, stop motor
// This variable keeps track of which of the above we are on
uint8_t motor_motion_phase[5];

void loop(void) {
    for(uint8_t motor_num = 0; motor_num < 5; motor_num++) {
        switch(motor_motion_phase[motor_num]) {
            case 0:
                // Check if motors have completed the first turn
                if (-encoders[motor_num].getCount() > ENCODER_TARGET_COUNT) {
                    Serial.print("Reversing motor ");
                    Serial.println(motor_num+1);

                    controller_write(motor_num+1, 0x30, 0x01);

                    motor_motion_phase[motor_num] += 1;
                }
                break;

            case 1:
                // Check if motor returned to initial position
                if (encoders[motor_num].getCount() > 0) {
                    Serial.print("Stopping motor ");
                    Serial.println(motor_num+1);
                    controller_write(motor_num+1, 0x00, 0x00);

                    motor_motion_phase[motor_num] += 1;
                }
                break;
        }
    }
    delay(10);
}

