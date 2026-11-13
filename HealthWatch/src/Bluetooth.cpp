#include "Bluetooth.h"
#include <BLE2902.h>

Bluetooth::Bluetooth() : callbacks(*this) {
  connected = false;
  snapshot = nullptr;
}

//Central connected
void Bluetooth::ServerCallbacks::onConnect(BLEServer* server) {
  owner.connected = true;
}

//Central left; advertise again so the phone can come back
void Bluetooth::ServerCallbacks::onDisconnect(BLEServer* server) {
  owner.connected = false;
  BLEDevice::startAdvertising();
}

//Start the stack, create the service and characteristic, and advertise
void Bluetooth::init(const char* deviceName) {
  BLEDevice::init(deviceName);

  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(&callbacks);

  BLEService* service = server->createService(SERVICE_UUID);
  snapshot = service->createCharacteristic(SNAPSHOT_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  snapshot->addDescriptor(new BLE2902());    //Client characteristic configuration, enables notify

  uint8_t empty[PACKET_SIZE] = {};
  snapshot->setValue(empty, PACKET_SIZE);
  service->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->start();
}

//Store the latest packet; notify only when someone is connected
void Bluetooth::publish(const uint8_t packet[PACKET_SIZE]) {
  if (!snapshot) return;
  snapshot->setValue((uint8_t*)packet, PACKET_SIZE);
  if (connected) snapshot->notify();
}

bool Bluetooth::isConnected() { return connected; }
