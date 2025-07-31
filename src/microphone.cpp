#include "microphone.h"
#include <Arduino.h>

bool setup_microphone() {
    Serial.println("Requesting I2S access for microphone...");
    
    if (!I2SManager::requestI2SAccess(I2SDevice::MICROPHONE)) {
        Serial.println("❌ Cannot setup microphone: I2S is busy");
        return false;
    }
    
    Serial.println("Initializing I2S for microphone...");
    esp_err_t err = I2SManager::initializeMicrophone();
    if (err != ESP_OK) {
        Serial.printf("❌ Failed to initialize microphone: %s\n", esp_err_to_name(err));
        I2SManager::releaseI2SAccess(I2SDevice::MICROPHONE);
        return false;
    }
    
    Serial.println("✅ Microphone initialized successfully!");
    return true;
}

esp_err_t read_microphone_data(int32_t* buffer, size_t buffer_size, size_t* bytes_read) {
    if (!I2SManager::hasI2SAccess(I2SDevice::MICROPHONE)) {
        Serial.println("❌ Cannot read microphone: No I2S access");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Use longer timeout for reliable data reading (1 second instead of 100ms)
    // TODO
    return i2s_read(I2SManager::I2S_PORT, buffer, buffer_size, bytes_read, 1000);
}

void stop_microphone() {
    if (I2SManager::hasI2SAccess(I2SDevice::MICROPHONE)) {
        Serial.println("Stopping microphone and releasing I2S access...");
        I2SManager::releaseI2SAccess(I2SDevice::MICROPHONE);
    }
}

bool is_microphone_active() {
    return I2SManager::hasI2SAccess(I2SDevice::MICROPHONE);
}