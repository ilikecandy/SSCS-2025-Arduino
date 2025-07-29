#include "i2s_manager.h"

// Static member definitions
I2SDevice I2SManager::currentDevice = I2SDevice::NONE;
bool I2SManager::initialized = false;

bool I2SManager::requestI2SAccess(I2SDevice device) {
    if (currentDevice != I2SDevice::NONE && currentDevice != device) {
        Serial.printf("❌ I2S access denied: Device %d is currently using I2S (requested by device %d)\n", 
                     (int)currentDevice, (int)device);
        return false;
    }
    
    if (currentDevice == device) {
        // Device already has access
        return true;
    }
    
    currentDevice = device;
    Serial.printf("✅ I2S access granted to device %d\n", (int)device);
    return true;
}

bool I2SManager::releaseI2SAccess(I2SDevice device) {
    if (currentDevice != device) {
        Serial.printf("❌ I2S release denied: Device %d doesn't have access (current: %d)\n", 
                     (int)device, (int)currentDevice);
        return false;
    }
    
    shutdownI2S();
    currentDevice = I2SDevice::NONE;
    Serial.printf("✅ I2S access released by device %d\n", (int)device);
    return true;
}

void I2SManager::forceReleaseI2SAccess() {
    if (currentDevice != I2SDevice::NONE) {
        Serial.printf("⚠️ Force releasing I2S access from device %d\n", (int)currentDevice);
        shutdownI2S();
        currentDevice = I2SDevice::NONE;
    }
}

bool I2SManager::hasI2SAccess(I2SDevice device) {
    return currentDevice == device;
}

I2SDevice I2SManager::getCurrentDevice() {
    return currentDevice;
}

esp_err_t I2SManager::initializeMicrophone() {
    if (!hasI2SAccess(I2SDevice::MICROPHONE)) {
        Serial.println("❌ Cannot initialize microphone: No I2S access");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (initialized) {
        shutdownI2S();
    }
    
    // Microphone I2S configuration
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 16000,  // TODO 48000???
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
        .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S | I2S_COMM_FORMAT_I2S_MSB),
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 1024,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    // Microphone pin configuration
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SERIAL_CLOCK,
        .ws_io_num = I2S_LEFT_RIGHT_CLOCK,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SERIAL_DATA
    };
    
    Serial.println("Installing I2S driver for microphone...");
    esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("❌ Failed installing I2S driver: %s\n", esp_err_to_name(err));
        return err;
    }
    
    err = i2s_set_pin(I2S_PORT, &pin_config);
    if (err != ESP_OK) {
        Serial.printf("❌ Failed setting I2S pins: %s\n", esp_err_to_name(err));
        i2s_driver_uninstall(I2S_PORT);
        return err;
    }
    
    err = i2s_start(I2S_PORT);
    if (err != ESP_OK) {
        Serial.printf("❌ Failed starting I2S: %s\n", esp_err_to_name(err));
        i2s_driver_uninstall(I2S_PORT);
        return err;
    }
    
    // Give microphone time to stabilize
    // TODO delay(500);
    delay(50);
    
    initialized = true;
    Serial.println("✅ I2S initialized for microphone");
    return ESP_OK;
}

esp_err_t I2SManager::initializeSpeaker() {
    if (!hasI2SAccess(I2SDevice::SPEAKER)) {
        Serial.println("❌ Cannot initialize speaker: No I2S access");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (initialized) {
        shutdownI2S();
    }
    
    // Speaker I2S configuration (from TTS.cpp)
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 1024,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    // Speaker pin configuration
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SERIAL_CLOCK,
        .ws_io_num = I2S_LEFT_RIGHT_CLOCK,
        .data_out_num = I2S_SERIAL_DATA,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    
    Serial.println("Installing I2S driver for speaker...");
    esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("❌ Failed installing I2S driver: %s\n", esp_err_to_name(err));
        return err;
    }
    
    err = i2s_set_pin(I2S_PORT, &pin_config);
    if (err != ESP_OK) {
        Serial.printf("❌ Failed setting I2S pins: %s\n", esp_err_to_name(err));
        i2s_driver_uninstall(I2S_PORT);
        return err;
    }
    
    // Clear DMA buffer
    i2s_zero_dma_buffer(I2S_PORT);
    
    // Start I2S
    err = i2s_start(I2S_PORT);
    if (err != ESP_OK) {
        Serial.printf("❌ Failed starting I2S: %s\n", esp_err_to_name(err));
        i2s_driver_uninstall(I2S_PORT);
        return err;
    }
    
    // Give speaker time to stabilize
    // TODO delay(500);
    delay(50);
    
    initialized = true;
    Serial.println("✅ I2S initialized for speaker");
    return ESP_OK;
}

void I2SManager::shutdownI2S() {
    if (initialized) {
        i2s_driver_uninstall(I2S_PORT);
        initialized = false;
        Serial.println("✅ I2S driver uninstalled");
    }
}

bool I2SManager::isInitialized() {
    return initialized;
}
