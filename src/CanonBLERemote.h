#ifndef CANON_BLE_REMOTE_H_
#define CANON_BLE_REMOTE_H_

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <ArduinoNvs.h>

// Fires (from the NimBLE task) for every advertisement seen.
class CanonScanCallbacks : public NimBLEScanCallbacks {
private:
    NimBLEUUID service_uuid_wanted;
    bool *pready_to_connect;
    NimBLEAddress *paddress_to_connect;
public:
    CanonScanCallbacks(NimBLEUUID service_uuid, bool *ready, NimBLEAddress *address);
    void onResult(NimBLEAdvertisedDevice *advertisedDevice) override;
};

// Connection-state tracking.
class CanonClientCallbacks : public NimBLEClientCallbacks {
private:
    bool connected = false;
public:
    void onConnect(NimBLEClient *pClient) override;
    void onDisconnect(NimBLEClient *pClient) override;
    bool isConnected();
};

// Security/pairing callbacks (mirror the original passkey behaviour).
class CanonSecurityCallbacks : public NimBLESecurityCallbacks {
public:
    uint32_t onPassKeyRequest(NimBLESecurity *pSecurity) override;
    bool onConfirmPIN(uint32_t pin) override;
    bool onSecurityRequest() override;
    void onAuthenticationComplete(NimBLEAuthComplete *pComplete) override;
};

class CanonBLERemote {
private:
    const uint8_t BUTTON_RELEASE = 0b10000000;
    const uint8_t BUTTON_FOCUS   = 0b01000000;
    const uint8_t BUTTON_TELE    = 0b00100000;
    const uint8_t BUTTON_WIDE    = 0b00010000;
    const uint8_t MODE_IMMEDIATE = 0b00001100;
    const uint8_t MODE_DELAY     = 0b00000100;
    const uint8_t MODE_MOVIE     = 0b00001000;

    const NimBLEUUID SERVICE_UUID;
    const NimBLEUUID PAIRING_SERVICE;
    const NimBLEUUID SHUTTER_CONTROL_SERVICE;

    NimBLEClient *pclient = nullptr;
    CanonClientCallbacks *pconnection_state = nullptr;
    CanonScanCallbacks *pScanCallbacks = nullptr;
    NimBLEAddress camera_address;
    NimBLERemoteService *pRemoteService = nullptr;
    NimBLECharacteristic *pRemoteCharacteristic_Pairing = nullptr;
    NimBLECharacteristic *pRemoteCharacteristic_Trigger = nullptr;
    ArduinoNvs nvs;

    bool ready_to_connect = false;
    bool cameraPaired = false;
    std::string device_name = "";

    void scan(unsigned int scan_duration);
    void disconnect();

public:
    CanonBLERemote(std::string name);
    void init();
    bool pair(unsigned int scan_duration);
    bool connect();          // direct connect to stored MAC (no scan) — public for boot auto-connect
    bool isConnected();
    bool trigger();
    bool focus();
    bool hasPairedCamera();
    NimBLEAddress getPairedAddress();
    std::string getPairedAddressString();
};

#endif
