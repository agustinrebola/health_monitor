#include "Bluetooth.h"

#define SERVICE_UUID  "f0a1c001-0cb6-4f90-9c6c-4af42ca31f01"
#define SNAPSHOT_UUID "f0a1c002-0cb6-4f90-9c6c-4af42ca31f01"

//Write a 32-bit value little-endian
static void put32(uint8_t* p, uint32_t v) {
  p[0] = v;
  p[1] = v >> 8;
  p[2] = v >> 16;
  p[3] = v >> 24;
}

Bluetooth::Bluetooth() : callbacks(*this) {
  connected = false;
  sequence = 0;
  snapshot = nullptr;
}

void Bluetooth::ServerCallbacks::onConnect(NimBLEServer* server, NimBLEConnInfo& info) {
  owner.connected = true;
}

void Bluetooth::ServerCallbacks::onDisconnect(NimBLEServer* server, NimBLEConnInfo& info, int reason) {
  owner.connected = false;                   //NimBLE starts advertising again by itself
}

//Start BLE, create the service and characteristic, advertise
void Bluetooth::init(const char* name) {
  NimBLEDevice::init(name);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(&callbacks);

  NimBLEService* service = server->createService(SERVICE_UUID);
  snapshot = service->createCharacteristic(SNAPSHOT_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  service->start();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->setName(name);
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->start();
}

//Build the 20-byte packet and notify the phone
//  0 version  1 flags  2 activity  3 quality  4-7 sequence  8-11 uptime ms
//  12-15 steps  16-17 cadence  18 heart rate  19 SpO2 (unused)
void Bluetooth::send(uint32_t steps, uint16_t cadence, bool walking,
                     uint8_t heartRate, bool heartRateValid, bool contact,
                     bool imuOk, bool ppgOk) {
  uint8_t p[20] = {};
  p[0] = 1;
  p[1] = (imuOk ? 1 : 0) | (ppgOk ? 2 : 0) | (heartRateValid ? 4 : 0) | (contact ? 8 : 0);
  p[2] = walking ? 2 : 1;                    //2 walking, 1 stationary
  p[3] = !contact ? 0 : (heartRateValid ? 3 : 2);   //0 no contact, 2 pulse seen, 3 rate valid
  put32(p + 4, sequence++);
  put32(p + 8, millis());
  put32(p + 12, steps);
  p[16] = cadence;
  p[17] = cadence >> 8;
  p[18] = heartRateValid ? heartRate : 0;

  snapshot->setValue(p, sizeof(p));
  if (connected) snapshot->notify();
}

bool Bluetooth::isConnected() { return connected; }
