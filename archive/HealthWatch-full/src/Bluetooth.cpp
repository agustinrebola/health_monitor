#include "Bluetooth.h"

Bluetooth::Bluetooth() : callbacks(*this) {
  connected = false;
  snapshot = nullptr;
}

//Central connected
void Bluetooth::ServerCallbacks::onConnect(NimBLEServer* server, NimBLEConnInfo& info) {
  owner.connected = true;
}

//Central left; NimBLE restarts advertising on its own (advertiseOnDisconnect)
void Bluetooth::ServerCallbacks::onDisconnect(NimBLEServer* server, NimBLEConnInfo& info, int reason) {
  owner.connected = false;
}

//Start the stack, create the service and characteristic, and advertise
void Bluetooth::init(const char* deviceName) {
  NimBLEDevice::init(deviceName);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(&callbacks);
  server->advertiseOnDisconnect(true);

  NimBLEService* service = server->createService(SERVICE_UUID);
  snapshot = service->createCharacteristic(SNAPSHOT_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  uint8_t empty[PACKET_SIZE] = {};
  snapshot->setValue(empty, PACKET_SIZE);
  service->start();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->setName(deviceName);
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->start();
}

//Store the latest packet; notify only when someone is connected
void Bluetooth::publish(const uint8_t packet[PACKET_SIZE]) {
  if (!snapshot) return;
  snapshot->setValue(packet, PACKET_SIZE);
  if (connected) snapshot->notify();
}

bool Bluetooth::isConnected() { return connected; }
