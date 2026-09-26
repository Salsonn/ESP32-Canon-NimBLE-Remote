#ifndef CANON_BLE_REMOTE_H_
#define CANON_BLE_REMOTE_H_

#include <Arduino.h>
#include <NimBLEDevice.h>   // pulls in Client, Scan, AdvertisedDevice, ConnInfo, etc.
#include <ArduinoNvs.h>

class CanonBLERemote;

// Client + security callbacks. In NimBLE 2.5.x the security/passkey hooks
// (onPassKeyDisplay, onConfirmPasskey, onAuthenticationComplete) live on the
// client-callbacks object — there is no separate security-callbacks class.
class CanonClientCallbacks : public NimBLEClientCallbacks {
public:
    bool connected = false;
    void     onConnect(NimBLEClient *pClient) override;
    void     onConnectFail(NimBLEClient *pClient, int reason) override;
    void     onDisconnect(NimBLEClient *pClient, int reason) override;   // note: 2 args in 2.5.x
    uint32_t onPassKeyDisplay(NimBLEConnInfo &connInfo) override;
    void     onConfirmPasskey(NimBLEConnInfo &connInfo, uint32_t pin) override;
    void     onAuthenticationComplete(NimBLEConnInfo &connInfo) override;
};

// Scan filter: matches the Canon remote service UUID (name "Canon" as fallback).
// 2.5.x renames the base to NimBLEScanCallbacks and passes a POINTER.
class CanonScanCallback : public NimBLEScanCallbacks {
public:
    CanonBLERemote *owner = nullptr;
    void onResult(const NimBLEAdvertisedDevice *adv) override;   // note: pointer
};

class CanonBLERemote {
private:
    // Trigger command bits (BR-E1 protocol)
    const byte BUTTON_RELEASE = 0b10000000;
    const byte BUTTON_FOCUS   = 0b01000000;
    const byte BUTTON_TELE    = 0b00100000;
    const byte BUTTON_WIDE    = 0b00010000;
    const byte MODE_IMMEDIATE = 0b00001100;
    const byte MODE_DELAY     = 0b00000100;
    const byte MODE_MOVIE     = 0b00001000;

    const NimBLEUUID SERVICE_UUID;
    const NimBLEUUID PAIRING_SERVICE;
    const NimBLEUUID SHUTTER_CONTROL_SERVICE;

    NimBLEClient *pclient = nullptr;
    CanonClientCallbacks *pconnection_state = nullptr;
    CanonScanCallback *pscan_callback = nullptr;
    NimBLEAddress camera_address;
    NimBLERemoteCharacteristic *pRemoteCharacteristic_Pairing = nullptr;
    NimBLERemoteCharacteristic *pRemoteCharacteristic_Trigger = nullptr;
    ArduinoNvs nvs;

    bool ready_to_connect = false;
    String device_name = "";

    bool handleAdvertised(const NimBLEAdvertisedDevice &adv);
    void scan(unsigned int scan_duration);
    void disconnect();

public:
    CanonBLERemote(String name);
    void init();
    bool pair(unsigned int scan_duration);
    bool connect();          // public: direct connect to stored MAC, no scan
    bool isConnected();
    bool hasPairedCamera();  // true if a camera MAC is stored in NVS
    bool trigger();
    bool focus();
    NimBLEAddress getPairedAddress();
    String getPairedAddressString();
};

#endif
