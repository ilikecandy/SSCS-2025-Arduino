#ifndef MICROPHONE_H
#define MICROPHONE_H

#include <driver/i2s.h>
#include "i2s_manager.h"

/**
 * @brief Initializes the I2S driver for the microphone.
 * @return true if initialization was successful, false otherwise.
 */
bool setup_microphone();

/**
 * @brief Reads audio data from the I2S microphone.
 * 
 * @param buffer The buffer to store the audio data.
 * @param buffer_size The size of the buffer.
 * @param bytes_read A pointer to store the number of bytes read.
 * @return esp_err_t The result of the I2S read operation.
 */
esp_err_t read_microphone_data(int32_t* buffer, size_t buffer_size, size_t* bytes_read);

/**
 * @brief Stops the microphone and releases I2S resources.
 */
void stop_microphone();

/**
 * @brief Checks if the microphone currently has I2S access.
 * @return true if the microphone can be used.
 */
bool is_microphone_active();

#endif
