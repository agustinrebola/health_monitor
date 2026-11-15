#include "MAX30102.h"

#define MAX30102_ADDR 0x57

static const uint8_t LED_CURRENT = 0x2C;          //44 = 8.8 mA per LED; best wrist result in testing

MAX30102::MAX30102() {
  ready = false;
  red = ir = 0;
}

//Write one register
bool MAX30102::writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MAX30102_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

//Read one register; 0 on failure
uint8_t MAX30102::readReg(uint8_t reg) {
  Wire.beginTransmission(MAX30102_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom(MAX30102_ADDR, 1) != 1) return 0;
  return Wire.read();
}

//Reset and start red + IR sampling at 100 Hz, 18-bit
bool MAX30102::init() {
  if (readReg(0xFF) != 0x15) return false;   //Part ID
  writeReg(0x09, 0x40);                      //Reset
  delay(10);
  ready = writeReg(0x04, 0) &&               //FIFO write pointer
          writeReg(0x05, 0) &&               //FIFO overflow counter
          writeReg(0x06, 0) &&               //FIFO read pointer
          writeReg(0x08, 0x00) &&            //No averaging, no rollover
          writeReg(0x0A, 0x27) &&            //100 samples/s, 411 us pulse, 18-bit
          writeReg(0x0C, LED_CURRENT) &&     //Red LED
          writeReg(0x0D, LED_CURRENT) &&     //IR LED
          writeReg(0x09, 0x03);              //SpO2 mode: red + IR
  return ready;
}

//Samples waiting in the 32-entry FIFO
uint8_t MAX30102::available() {
  if (!ready) return 0;
  const uint8_t wr = readReg(0x04);
  const uint8_t rd = readReg(0x06);
  return (wr - rd) & 0x1F;
}

//Read one sample from the FIFO into red and ir
bool MAX30102::read() {
  Wire.beginTransmission(MAX30102_ADDR);
  Wire.write(0x07);                          //FIFO data register
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MAX30102_ADDR, 6) != 6) return false;

  uint8_t b[6];
  for (int i = 0; i < 6; i++)
    b[i] = Wire.read();

  red = (((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2]) & 0x3FFFF;
  ir = (((uint32_t)b[3] << 16) | ((uint32_t)b[4] << 8) | b[5]) & 0x3FFFF;
  return true;
}
