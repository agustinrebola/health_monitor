#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "Packet.h"

//One GATT service with one read/notify characteristic, built on NimBLE-Arduino
//(library manager: "NimBLE-Arduino", version 2.x). NimBLE adds the client
//configuration descriptor itself and re-advertises after a disconnect
class Bluetooth{
  private:
    //Connection events from the library
    class ServerCallbacks : public NimBLEServerCallbacks {
      private:
        Bluetooth& owner;
      public:
        ServerCallbacks(Bluetooth& b) : owner(b) {}
        void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override;
        void onDisconnect(NimBLEServer* server, NimBLEConnInfo& info, int reason) override;
    };

    bool connected;                          //A central is connected
    NimBLECharacteristic* snapshot;          //Snapshot characteristic
    ServerCallbacks callbacks;               //Keeps connected up to date

  public:
    Bluetooth();

    void init(const char* deviceName);
    void publish(const uint8_t packet[PACKET_SIZE]);

    bool isConnected();
};

#endif
