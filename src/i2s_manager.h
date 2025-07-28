#ifndef I2S_MANAGER_H
#define I2S_MANAGER_H

#include <driver/i2s.h>
#include <Arduino.h>

enum class I2SDevice {
    MICROPHONE,
    SPEAKER,
    NONE
};

class I2SManager {
private:
    static I2SDevice currentDevice;
    static bool initialized;
    
public:
    static const i2s_port_t I2S_PORT = I2S_NUM_1;
    /**
     * @brief Requests exclusive access to the I2S port for a specific device
     * @param device The device requesting access
     * @return true if access was granted, false if another device is using it
     */
    static bool requestI2SAccess(I2SDevice device);
    
    /**
     * @brief Releases I2S access from the current device
     * @param device The device releasing access (must match current device)
     * @return true if access was successfully released
     */
    static bool releaseI2SAccess(I2SDevice device);
    
    /**
     * @brief Forces release of I2S access (emergency use)
     */
    static void forceReleaseI2SAccess();
    
    /**
     * @brief Checks if a specific device currently has I2S access
     * @param device The device to check
     * @return true if the device has access
     */
    static bool hasI2SAccess(I2SDevice device);
    
    /**
     * @brief Gets the device that currently has I2S access
     * @return The current device or I2SDevice::NONE if no device has access
     */
    static I2SDevice getCurrentDevice();
    
    /**
     * @brief Initializes I2S for microphone use
     * @return ESP_OK if successful
     */
    static esp_err_t initializeMicrophone();
    
    /**
     * @brief Initializes I2S for speaker use
     * @return ESP_OK if successful
     */
    static esp_err_t initializeSpeaker();
    
    /**
     * @brief Shuts down the I2S driver
     */
    static void shutdownI2S();
    
    /**
     * @brief Checks if I2S is currently initialized
     * @return true if initialized
     */
    static bool isInitialized();
};

#endif // I2S_MANAGER_H
