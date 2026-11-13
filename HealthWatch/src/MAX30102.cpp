#include "MAX30102.h"

#define MAX30102_ADDR 0x57

//Register map
static const uint8_t REG_FIFO_WR_PTR = 0x04;
static const uint8_t REG_OVF_COUNTER = 0x05;
static const uint8_t REG_FIFO_RD_PTR = 0x06;
static const uint8_t REG_FIFO_DATA = 0x07;
static const uint8_t REG_FIFO_CONFIG = 0x08;
static const uint8_t REG_MODE_CONFIG = 0x09;
static const uint8_t REG_SPO2_CONFIG = 0x0A;
static const uint8_t REG_LED1_PA = 0x0C;
static const uint8_t REG_LED2_PA = 0x0D;
static const uint8_t REG_PART_ID = 0xFF;

//Mode register values
static const uint8_t MODE_RESET = 0x40;
static const uint8_t MODE_SPO2 = 0x03;            //Red and IR sampling
static const uint8_t MODE_SPO2_SHUTDOWN = 0x83;   //Sampling paused

static const uint8_t SPO2_CONFIG = 0x27;          //18-bit ADC, 100 sps, 411 us pulse width
static const uint8_t FIFO_CONFIG = 0x00;          //No averaging, no rollover
static const uint8_t FIFO_CHUNK = 5;              //5 samples x 6 bytes fits Wire's 32-byte buffer
static const uint32_t ADC_MASK = 0x3FFFF;         //18-bit sample

MAX30102::MAX30102() {
  bus = nullptr;
  ready = false;
  errors = overflows = 0;
  ledCurrent = LED_WRIST;
}

//Write one register, counting bus failures
bool MAX30102::writeReg(uint8_t reg, uint8_t value) {
  bus->beginTransmission(MAX30102_ADDR);
  bus->write(reg);
  bus->write(value);
  if (bus->endTransmission() == 0) return true;
  ++errors;
  return false;
}

//Read one register, counting bus failures
bool MAX30102::readReg(uint8_t reg, uint8_t& value) {
  bus->beginTransmission(MAX30102_ADDR);
  bus->write(reg);
  if (bus->endTransmission(false) != 0 || bus->requestFrom((uint8_t)MAX30102_ADDR, (uint8_t)1) != 1) {
    ++errors;
    return false;
  }
  value = bus->read();
  return true;
}

//Read a run of FIFO bytes, counting bus failures
bool MAX30102::readFifo(uint8_t* bytes, uint8_t count) {
  bus->beginTransmission(MAX30102_ADDR);
  bus->write(REG_FIFO_DATA);
  if (bus->endTransmission(false) != 0 || bus->requestFrom((uint8_t)MAX30102_ADDR, count) != count) {
    ++errors;
    return false;
  }
  for (uint8_t i = 0; i < count; i++)
    bytes[i] = bus->read();
  return true;
}

//Reset FIFO pointers and forget the timestamp phase
void MAX30102::clearFifo() {
  writeReg(REG_FIFO_WR_PTR, 0);
  writeReg(REG_OVF_COUNTER, 0);
  writeReg(REG_FIFO_RD_PTR, 0);
  clock.reset();
}

//Reset and configure the sensor for red and IR sampling at 100 Hz
bool MAX30102::init(TwoWire& wire) {
  bus = &wire;
  ready = false;
  clock.reset();

  uint8_t part = 0;
  if (!readReg(REG_PART_ID, part) || part != 0x15) return false;
  if (!writeReg(REG_MODE_CONFIG, MODE_RESET)) return false;
  delay(10);                                 //Let the reset bit self-clear

  ready = writeReg(REG_FIFO_WR_PTR, 0) &&
          writeReg(REG_OVF_COUNTER, 0) &&
          writeReg(REG_FIFO_RD_PTR, 0) &&
          writeReg(REG_FIFO_CONFIG, FIFO_CONFIG) &&
          writeReg(REG_SPO2_CONFIG, SPO2_CONFIG) &&
          writeReg(REG_LED1_PA, ledCurrent) &&
          writeReg(REG_LED2_PA, ledCurrent) &&
          writeReg(REG_MODE_CONFIG, MODE_SPO2);
  return ready;
}

//Change both LED currents; sampling pauses while the FIFO is cleared so the
//overflow counter cannot race the producer
bool MAX30102::setLedCurrent(uint8_t value) {
  if (!ready) return false;
  if (value == ledCurrent) return true;

  if (!writeReg(REG_MODE_CONFIG, MODE_SPO2_SHUTDOWN)) return false;

  const bool configured = writeReg(REG_LED1_PA, value) &&
                          writeReg(REG_LED2_PA, value) &&
                          writeReg(REG_FIFO_WR_PTR, 0) &&
                          writeReg(REG_OVF_COUNTER, 0) &&
                          writeReg(REG_FIFO_RD_PTR, 0) &&
                          writeReg(REG_MODE_CONFIG, MODE_SPO2);
  if (!configured) {
    ready = false;
    return false;
  }

  ledCurrent = value;
  clock.reset();
  return true;
}

//Read every pending FIFO sample and hand each one to sink; returns the count
uint8_t MAX30102::drain(uint32_t nowUs, PpgSink sink) {
  if (!ready) return 0;

  uint8_t wr = 0, rd = 0, ovf = 0;
  if (!readReg(REG_FIFO_WR_PTR, wr) || !readReg(REG_FIFO_RD_PTR, rd) || !readReg(REG_OVF_COUNTER, ovf))
    return 0;

  //Lost FIFO phase: return to a known state rather than assign false timestamps
  if (ovf) {
    overflows += ovf;
    clearFifo();
    return 0;
  }

  uint8_t available = (wr - rd) & 0x1F;      //32-entry circular FIFO
  clock.startBatch(nowUs, available);

  uint8_t readCount = 0;
  while (available) {
    const uint8_t chunk = available > FIFO_CHUNK ? FIFO_CHUNK : available;
    uint8_t bytes[FIFO_CHUNK * 6];

    //A partial transfer may have advanced the read pointer; discard the phase
    if (!readFifo(bytes, chunk * 6)) {
      clearFifo();
      break;
    }

    for (uint8_t i = 0; i < chunk; i++) {
      const uint8_t* p = bytes + i*6;
      PpgSample sample = {};
      sample.red = (((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]) & ADC_MASK;
      sample.ir = (((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 8) | p[5]) & ADC_MASK;
      sample.timestampUs = clock.next();
      sample.valid = true;
      sink(sample);
      ++readCount;
    }
    available -= chunk;
  }
  return readCount;
}

bool MAX30102::isReady() { return ready; }
uint8_t MAX30102::getLedCurrent() { return ledCurrent; }
uint32_t MAX30102::getErrors() { return errors; }
uint32_t MAX30102::getOverflows() { return overflows; }
