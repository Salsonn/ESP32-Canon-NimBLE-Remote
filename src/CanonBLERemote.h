#ifndef CANON_BLE_REMOTE_H_
#define CANON_BLE_REMOTE_H_

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <ArduinoNvs.h>

class CanonBLERemote;

// Tracks client connection state (replaces the classic stack's ConnectivityState)
class CanonClientCallbacks : public NimBLEClientCallbacks {
public:
    bool connected = false;
    void onConnect(NimBLEClient *pClient) override;
    void onDisconnect(NimBLEClient *pClient) override;
};

// Mirrors the original SecurityCallback: passkey 123456, auto-confirm PIN
class CanonSecurityCallbacks : public NimBLEAuthenticationCallbacks {
public:
    uint32_t onPassKeyRequest(NimBLEDevice *pDevice) override;
    bool onConfirmPIN(NimBLEDevice *pDevice) override;
    void onSecurityStatus(NimBLEDevice *pDevice, NimBLESecurityStatus status) override;
};

// Scan filter: matches the Canon remote service UUID (name "Canon" as fallback)
class CanonScanCallback : public NimBLEAdvertisedDeviceCallbacks {
public:
    CanonBLERemote *owner = nullptr;
    void onResult(const NimBLEAdvertisedDevice &adv) override;
};

class CanonBLERemote {
private:
    // Trigger command bits (BR-E1 protocol)
    const byte BUTTON_RELEASE = 0b10000000;
    const byte BUTTON_FOCUS = 0b01000000;
    const byte BUTTON_TELE = 0b00100000;
    const byte BUTTON_WIDE = 0b00010000;
    const byte MODE_IMMEDIATE = 0b00001100;
    const byte MODE_DELAY = 0b00000100;
    const byte MODE_MOVIE = 0b00001000;

    const NimBLEUUID SERVICE_UUID;
    const NimBLEUUID PAIRING_SERVICE;
    const NimBLEUUID SHUTTER_CONTROL_SERVICE;

    // Created in init(): NimBLE requires NimBLEDevice::init() before createClient()
    NimBLEClient *pclient = nullptr;
    CanonClientCallbacks *pconnection_state = nullptr;
    CanonSecurityCallbacks *psecurity_callbacks = nullptr;
    CanonScanCallback *pscan_callback = nullptr;
    NimBLEAddress camera_address;
    NimBLECharacteristic *pRemoteCharacteristic_Pairing = nullptr;
    NimBLECharacteristic *pRemoteCharacteristic_Trigger = nullptr;
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
    bool connect();   // public (was private in the original): direct connect to stored MAC, no scan
    bool isConnected();
    bool trigger();
    bool focus();
    NimBLEAddress getPairedAddress();
    String getPairedAddressString();
};

#endif
