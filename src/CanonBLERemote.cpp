#include "CanonBLERemote.h"

static const char *LOG_TAG = "CanoNimBLE";

// ---------------------------------------------------------------- callbacks

void CanonClientCallbacks::onConnect(NimBLEClient *pClient) {
    connected = true;
    log_i("Connected to camera");
}

void CanonClientCallbacks::onConnectFail(NimBLEClient *pClient, int reason) {
    log_w("Connect failed, reason %d", reason);
}

void CanonClientCallbacks::onDisconnect(NimBLEClient *pClient, int reason) {
    connected = false;
    log_i("Disconnected from camera, reason %d", reason);
}

uint32_t CanonClientCallbacks::onPassKeyDisplay(NimBLEConnInfo &connInfo) {
    log_i("Passkey display -> 123456");
    return 123456;
}

void CanonClientCallbacks::onConfirmPasskey(NimBLEConnInfo &connInfo, uint32_t pin) {
    // Auto-accept. Do NOT vTaskDelay here: this runs on the NimBLE host task,
    // and blocking it stalls all BLE processing.
    log_i("Confirm passkey %u -> accept", (unsigned)pin);
}

void CanonClientCallbacks::onAuthenticationComplete(NimBLEConnInfo &connInfo) {
    log_i("Authentication complete");
}

void CanonScanCallback::onResult(const NimBLEAdvertisedDevice *adv) {
    if (owner && adv) owner->handleAdvertised(*adv);
}

// ------------------------------------------------------------------- class

CanonBLERemote::CanonBLERemote(String name)
    : SERVICE_UUID("00050000-0000-1000-0000-d8492fffa821"),
      PAIRING_SERVICE("00050002-0000-1000-0000-d8492fffa821"),
      SHUTTER_CONTROL_SERVICE("00050003-0000-1000-0000-d8492fffa821")
{
    device_name = name;
}

void CanonBLERemote::init() {
    if (!NimBLEDevice::isInitialized()) {
        // Configure BEFORE init() so the settings take effect at bring-up.
        // 2.5.x API: setSecurityAuth(bonding, mitm, sc) + setSecurityPasskey().
        NimBLEDevice::setSecurityAuth(true, true, true);   // bonding + MITM + SC
        NimBLEDevice::setSecurityPasskey(123456);
        if (!NimBLEDevice::init(device_name.c_str())) {
            log_e("NimBLE init failed");
            return;
        }
        NimBLEDevice::setPower(0);   // cap TX power at 0 dBm; camera is ~1-3 m away
        log_i("NimBLE ready");
    }

    if (pclient == nullptr) {
        pclient = NimBLEDevice::createClient();
        pclient->setConnectTimeout(10);   // fail fast if the camera is off
        pconnection_state = new CanonClientCallbacks();
        pclient->setClientCallbacks(pconnection_state);
    }

    if (pscan_callback == nullptr) {
        pscan_callback = new CanonScanCallback();
        pscan_callback->owner = this;
    }

    if (nvs.begin()) {
        String address = nvs.getString("cameraaddr");
        if (address.length() == 17) {
            camera_address = NimBLEAddress(address.c_str(), 0);   // 0 = public address type
            log_i("Paired camera address: %s", address.c_str());
        } else {
            log_i("No camera paired yet");
        }
    } else {
        log_e("NVS init failed");
    }
}

// Scan for the camera advertising the Canon remote service UUID.
// The scanner only ever runs during pair() — never in the background.
void CanonBLERemote::scan(unsigned int scan_duration) {
    log_i("Start BLE scan");
    NimBLEScan *pBLEScan = NimBLEDevice::getScan();
    pBLEScan->setScanCallbacks(pscan_callback);   // 2.5.x name (was setAdvertisedDeviceCallbacks)
    pBLEScan->setActiveScan(true);
    pBLEScan->start(scan_duration, false);        // finite scan, auto-stops
}

bool CanonBLERemote::handleAdvertised(const NimBLEAdvertisedDevice &adv) {
    bool match = adv.isAdvertisingService(SERVICE_UUID);
    if (!match && adv.getName().find("Canon") == 0) match = true;   // fallback
    if (match) {
        camera_address = adv.getAddress();
        ready_to_connect = true;
        NimBLEDevice::getScan()->stop();
    }
    return match;
}

NimBLEAddress CanonBLERemote::getPairedAddress() {
    return camera_address;
}

String CanonBLERemote::getPairedAddressString() {
    return String(camera_address.toString().c_str());
}

bool CanonBLERemote::hasPairedCamera() {
    return !camera_address.isNull();
}

/**
 * Scan and pair a camera. Call only when pairing a new/changed camera
 * (camera in Bluetooth Function -> Remote -> pairing screen).
 * The camera MAC is stored in NVS for later direct connects.
 */
bool CanonBLERemote::pair(unsigned int scan_duration) {
    ready_to_connect = false;
    log_i("Scanning for camera...");
    scan(scan_duration);

    unsigned long start_ms = millis();
    while (!ready_to_connect && millis() - start_ms < (unsigned long)scan_duration * 1000UL) {
        delay(10);   // was an empty spin-loop in the original
    }

    if (!ready_to_connect) {
        log_i("Camera not found");
        return false;
    }
    log_i("Canon device found: %s", camera_address.toString().c_str());

    log_i("Pairing..");
    if (pclient->connect(camera_address)) {
        NimBLERemoteService *pRemoteService = pclient->getService(SERVICE_UUID);
        if (pRemoteService != nullptr) {
            pRemoteCharacteristic_Pairing = pRemoteService->getCharacteristic(PAIRING_SERVICE);
            if (pRemoteCharacteristic_Pairing != nullptr) {
                // Pairing payload: 0x03 followed by " name " (leading space replaced)
                std::string name_ = device_name.c_str();   // Arduino String -> std::string
                name_ = " " + name_ + " ";
                std::vector<uint8_t> payload(name_.size());
                payload[0] = 0x03;
                for (size_t i = 1; i < name_.size(); i++) payload[i] = (uint8_t)name_[i];
                pRemoteCharacteristic_Pairing->writeValue(payload, false);
                log_i("Camera pairing success");
                delay(200);
                disconnect();
                delay(200);
                if (connect()) {
                    if (nvs.setString("cameraaddr", camera_address.toString().c_str()) && nvs.commit()) {
                        log_i("Saved camera address to NVS");
                        return true;
                    }
                    log_e("Storing camera address in NVS failed");
                    return false;
                }
                return false;
            }
            log_e("Couldn't acquire the pairing service");
        } else {
            log_e("Couldn't acquire the remote main service");
        }
    } else {
        log_e("Couldn't connect to the camera");
    }
    return false;
}

/**
 * Direct connect to the stored camera MAC — no scan, no pairing handshake.
 * Used at boot and by trigger()/focus() auto-reconnect.
 */
bool CanonBLERemote::connect() {
    if (pclient == nullptr || camera_address.isNull()) return false;

    if (pclient->connect(camera_address)) {
        // Slow the connection interval: radio heartbeats ~1-2x/s instead of
        // ~20-30x/s. Units: intervals in 1.25 ms, timeout in 10 ms (500 = 5 s).
        // The camera may reject the request; negotiated params then stay.
        pclient->updateConnParams(400, 800, 0, 500);

        NimBLERemoteService *pRemoteService = pclient->getService(SERVICE_UUID);
        if (pRemoteService != nullptr) {
            pRemoteCharacteristic_Trigger = pRemoteService->getCharacteristic(SHUTTER_CONTROL_SERVICE);
            if (pRemoteCharacteristic_Trigger != nullptr) {
                log_i("Camera connection success");
                return true;
            }
            log_e("Get trigger service failed");
        } else {
            log_e("Couldn't acquire the remote main service");
        }
        disconnect();
    }
    return false;
}

void CanonBLERemote::disconnect() {
    if (pclient) pclient->disconnect();
}

bool CanonBLERemote::isConnected() {
    return pconnection_state != nullptr && pconnection_state->connected;
}

bool CanonBLERemote::trigger() {
    if (!isConnected() && !connect()) return false;

    uint8_t cmd = MODE_IMMEDIATE | BUTTON_RELEASE;
    pRemoteCharacteristic_Trigger->writeValue(&cmd, 1, false);
    delay(200);
    uint8_t release = MODE_IMMEDIATE;
    pRemoteCharacteristic_Trigger->writeValue(&release, 1, false);
    delay(50);
    return true;
}

bool CanonBLERemote::focus() {
    if (!isConnected() && !connect()) return false;

    uint8_t cmd = MODE_IMMEDIATE | BUTTON_FOCUS;
    pRemoteCharacteristic_Trigger->writeValue(&cmd, 1, false);
    delay(200);
    uint8_t release = MODE_IMMEDIATE;
    pRemoteCharacteristic_Trigger->writeValue(&release, 1, false);
    return true;
}
