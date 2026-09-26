#include "CanonBLERemote.h"
#include <esp_bt.h>   // esp_ble_tx_power_set (controller-level; works under NimBLE on ESP32)

static const char *LOG_TAG = "CanonBLE";

CanonScanCallbacks::CanonScanCallbacks(NimBLEUUID service_uuid, bool *ready, NimBLEAddress *address)
    : service_uuid_wanted(service_uuid), pready_to_connect(ready), paddress_to_connect(address) {}

void CanonScanCallbacks::onResult(NimBLEAdvertisedDevice *advertisedDevice) {
    if (advertisedDevice->haveServiceUUID()) {
        if (service_uuid_wanted.equals(advertisedDevice->getServiceUUID())) {
            *paddress_to_connect = advertisedDevice->getAddress();
            *pready_to_connect = true;
            NimBLEDevice::getScan()->stop();
        }
    }
}

void CanonClientCallbacks::onConnect(NimBLEClient *pClient)    { connected = true; }
void CanonClientCallbacks::onDisconnect(NimBLEClient *pClient) { connected = false; }
bool CanonClientCallbacks::isConnected()                        { return connected; }

uint32_t CanonSecurityCallbacks::onPassKeyRequest(NimBLESecurity *pSecurity) { return 123456; }
bool CanonSecurityCallbacks::onConfirmPIN(uint32_t pin) { (void)pin; return true; }
bool CanonSecurityCallbacks::onSecurityRequest() { return true; }
void CanonSecurityCallbacks::onAuthenticationComplete(NimBLEAuthComplete *pComplete) {
    if (pComplete->success) ESP_LOGI(LOG_TAG, "Pairing success");
    else ESP_LOGE(LOG_TAG, "Pairing failed (status %d)", pComplete->status);
}

CanonBLERemote::CanonBLERemote(std::string name)
    : SERVICE_UUID("00050000-0000-1000-0000-d8492fffa821"),
      PAIRING_SERVICE("00050002-0000-1000-0000-d8492fffa821"),
      SHUTTER_CONTROL_SERVICE("00050003-0000-1000-0000-d8492fffa821")
{
    device_name = name;
}

void CanonBLERemote::init() {
    NimBLEDevice::init(device_name.c_str());

    // Bond + MITM + Secure Connections; auto-answer passkey requests with 123456.
    NimBLEDevice::setSecurityIOCap(ESP_IO_CAP_KEYBOARD);
    NimBLEDevice::setSecurityAuth(ESP_LE_AUTH_BOND | ESP_LE_AUTH_MITM | ESP_LE_AUTH_SC);
    NimBLEDevice::setSecurityPasskey(123456);
    NimBLEDevice::setSecurityInitEncrypted(true);
    NimBLEDevice::setSecuritySC(true);
    NimBLEDevice::setSecurityBonds(true);
    NimBLEDevice::setSecurityCallbacks(new CanonSecurityCallbacks());

    pclient = NimBLEDevice::createClient();
    pconnection_state = new CanonClientCallbacks();
    pclient->setClientCallbacks(pconnection_state);
    pScanCallbacks = new CanonScanCallbacks(SERVICE_UUID, &ready_to_connect, &camera_address);

    // Cap TX power: camera is ~1-3 m away, 0 dBm is plenty.
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_CONN_HDL, ESP_BLE_PWR_TYPE_0DBM);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV,     ESP_BLE_PWR_TYPE_0DBM);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN,    ESP_BLE_PWR_TYPE_0DBM);

    if (nvs.begin()) {
        std::string address = nvs.getString("cameraaddr").c_str();
        if (address.length() == 17) {
            camera_address = NimBLEAddress::fromString(address);
            cameraPaired = true;
            ESP_LOGI(LOG_TAG, "Paired camera: %s", address.c_str());
        } else {
            ESP_LOGI(LOG_TAG, "No camera paired yet.");
        }
    } else {
        ESP_LOGE(LOG_TAG, "NVS init failed");
    }
}

void CanonBLERemote::scan(unsigned int scan_duration) {
    ESP_LOGI(LOG_TAG, "Start BLE scan");
    NimBLEScan *pBLEScan = NimBLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(pScanCallbacks);
    pBLEScan->setActiveScan(true);
    ready_to_connect = false;
    pBLEScan->start(scan_duration, false, true);   // timed scan, notify per result
}

NimBLEAddress CanonBLERemote::getPairedAddress() { return camera_address; }
std::string CanonBLERemote::getPairedAddressString() { return camera_address.toString(); }
bool CanonBLERemote::hasPairedCamera() { return cameraPaired; }

bool CanonBLERemote::pair(unsigned int scan_duration) {
    ESP_LOGI(LOG_TAG, "Scanning for camera...");
    scan(scan_duration);
    unsigned long start_ms = millis();
    while (!ready_to_connect && millis() - start_ms < scan_duration * 1000) {
        delay(10);   // was an empty spin in the classic version
    }

    if (!ready_to_connect) {
        ESP_LOGI(LOG_TAG, "Camera not found");
        return false;
    }
    ESP_LOGI(LOG_TAG, "Canon device found: %s", camera_address.toString().c_str());

    if (pclient->connect(camera_address)) {   // triggers the passkey pairing exchange
        pRemoteService = pclient->getServiceByUUID(SERVICE_UUID);
        if (pRemoteService != nullptr) {
            pRemoteCharacteristic_Pairing = pRemoteService->getCharacteristic(PAIRING_SERVICE);
            if (pRemoteCharacteristic_Pairing != nullptr) {
                // Register this remote with the camera: 0x03 + space-padded name.
                std::string name_ = " " + device_name + " ";
                std::string payload(name_.size(), 0);
                payload[0] = 0x03;
                for (size_t i = 1; i < name_.size(); i++) payload[i] = name_[i];
                pRemoteCharacteristic_Pairing->write(payload, false);
                ESP_LOGI(LOG_TAG, "Pairing write sent");
                delay(200);
                disconnect();
                delay(200);
                connect();   // reconnect now that the camera has bonded us
                nvs.setString("cameraaddr", camera_address.toString());
                if (nvs.commit()) {
                    ESP_LOGI(LOG_TAG, "Saved camera address");
                    return true;
                }
                ESP_LOGE(LOG_TAG, "Saving camera address failed");
                return false;
            }
            ESP_LOGE(LOG_TAG, "Couldn't acquire pairing service");
        } else {
            ESP_LOGE(LOG_TAG, "Couldn't acquire remote main service");
        }
    } else {
        ESP_LOGE(LOG_TAG, "Couldn't connect to camera");
    }
    return false;
}

bool CanonBLERemote::connect() {
    if (pclient->connect(camera_address)) {
        pRemoteService = pclient->getServiceByUUID(SERVICE_UUID);
        if (pRemoteService != nullptr) {
            pRemoteCharacteristic_Trigger = pRemoteService->getCharacteristic(SHUTTER_CONTROL_SERVICE);
            if (pRemoteCharacteristic_Trigger != nullptr) {
                ESP_LOGI(LOG_TAG, "Camera connection success");
                // Long connection interval so the radio (and CPU) sleep between events.
                NimBLEDevice::updateConnectionParameters(
                    pclient->getConnId(),
                    400,    // min 500 ms  (1.25 ms units)
                    800,    // max 1000 ms
                    0,      // latency
                    5000);  // supervision timeout (ms)
                return true;
            }
            ESP_LOGE(LOG_TAG, "Get trigger service failed");
        } else {
            ESP_LOGE(LOG_TAG, "Couldn't acquire remote main service");
        }
        disconnect();
    }
    return false;
}

void CanonBLERemote::disconnect() { pclient->disconnect(); }

bool CanonBLERemote::isConnected() { return pconnection_state->isConnected(); }

bool CanonBLERemote::trigger() {
    if (!isConnected() && !connect()) return false;
    uint8_t cmd = MODE_IMMEDIATE | BUTTON_RELEASE;
    pRemoteCharacteristic_Trigger->write(std::string(1, (char)cmd), false);
    delay(200);
    pRemoteCharacteristic_Trigger->write(std::string(1, (char)MODE_IMMEDIATE), false);
    delay(50);
    return true;
}

bool CanonBLERemote::focus() {
    if (!isConnected() && !connect()) return false;
    uint8_t cmd = MODE_IMMEDIATE | BUTTON_FOCUS;
    pRemoteCharacteristic_Trigger->write(std::string(1, (char)cmd), false);
    delay(200);
    pRemoteCharacteristic_Trigger->write(std::string(1, (char)MODE_IMMEDIATE), false);
    return true;
}
