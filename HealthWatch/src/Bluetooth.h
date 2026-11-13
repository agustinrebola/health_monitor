#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLECharacteristic.h>
#include "Packet.h"

//One GATT service with one notify characteristic, built on the ESP32 BLE library
class Bluetooth{
  private:
    //Connection events from the library
    class ServerCallbacks : public BLEServerCallbacks {
      private:
        Bluetooth& owner;
      public:
        ServerCallbacks(Bluetooth& b) : owner(b) {}
        void onConnect(BLEServer* server) override;
        void onDisconnect(BLEServer* server) override;
    };

    bool connected;                          //A central is subscribed
    BLECharacteristic* snapshot;             //Snapshot characteristic
    ServerCallbacks callbacks;               //Keeps connected up to date

  public:
    Bluetooth();

    void init(const char* deviceName);
    void publish(const uint8_t packet[PACKET_SIZE]);

    bool isConnected();
};

#endif
