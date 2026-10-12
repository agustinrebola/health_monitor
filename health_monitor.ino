#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

volatile bool tick10ms = false;
hw_timer_t *timer = NULL;
Adafruit_MPU6050 mpu;

void ARDUINO_ISR_ATTR onTimer(){
  tick10ms = true;
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  if (!mpu.begin()) {
    Serial.println("MPU6050 not found!");
    while (1) {
      delay(10);
    }
  }

  Serial.println("MPU6050 connected!");

  timer = timerBegin(1000000);          // 1 MHz timer
  timerAttachInterrupt(timer, &onTimer);
  timerAlarm(timer, 10000, true, 0);    // Trigger every 10 ms
}



void loop() {
    if (tick10ms){
        tick10ms = false;
        sensors_event_t accel;
        sensors_event_t gyro;
        sensors_event_t temp;

        mpu.getEvent(&accel, &gyro, &temp);

        Serial.print("Accel X: ");
        Serial.print(accel.acceleration.x);
        Serial.print(" Y: ");
        Serial.print(accel.acceleration.y);
        Serial.print(" Z: ");
        Serial.println(accel.acceleration.z);

        Serial.print("Gyro X: ");
        Serial.print(gyro.gyro.x);
        Serial.print(" Y: ");
        Serial.print(gyro.gyro.y);
        Serial.print(" Z: ");
        Serial.println(gyro.gyro.z);

        Serial.println();
    }
}