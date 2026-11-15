#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <Arduino.h>
#include <NimBLEDevice.h>

//One BLE service with one 20-byte notify characteristic, using NimBLE-Arduino.
//The packet layout is what the iPhone app expects
class Bluetooth{
  private:
    class ServerCallbacks : public NimBLEServerCallbacks {
      private:
        Bluetooth& owner;
      public:
        ServerCallbacks(Bluetooth& b) : owner(b) {}
        void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override;
        void onDisconnect(NimBLEServer* server, NimBLEConnInfo& info, int reason) override;
    };

    bool connected;                          //A phone is connected
    uint32_t sequence;                       //Packet counter
    NimBLECharacteristic* snapshot;          //The characteristic the phone reads
    ServerCallbacks callbacks;

  public:
    Bluetooth();

    void init(const char* name);
    void send(uint32_t steps, uint16_t cadence, bool walking,
              uint8_t heartRate, bool heartRateValid, bool contact,
              bool imuOk, bool ppgOk);

    bool isConnected();
};

#endif
