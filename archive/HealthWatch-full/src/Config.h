#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

//I2C bus
static const int SDA_PIN = 21;                    //I2C data pin
static const int SCL_PIN = 22;                    //I2C clock pin
static const uint32_t I2C_HZ = 400000;            //I2C clock speed

//Scheduler periods
static const uint32_t IMU_PERIOD_US = 10000;      //IMU sample slot, 100 Hz
static const uint32_t PPG_POLL_US = 20000;        //MAX30102 FIFO poll, 50 Hz
static const uint32_t BLE_PERIOD_MS = 500;        //Snapshot notify, 2 Hz so steps feel live
static const uint32_t STATUS_PERIOD_MS = 5000;    //Serial diagnostic status line
static const uint32_t LIVE_PERIOD_MS = 100;       //Serial live sensor readout, 10 Hz
static const uint32_t RETRY_PERIOD_MS = 5000;     //Sensor re-init attempt
static const uint32_t SENSOR_STALE_US = 2000000;  //No sample for this long means sensor lost

#endif
