#include "TTS.h"
#include "secrets.h"

const char* TTS::DEEPGRAM_URL = "https://api.deepgram.com/v1/speak?encoding=linear16&sample_rate=48000&model=aura-asteria-en";

TTS::TTS() : i2sInitialized(false) {
}

TTS::~TTS() {
    if (i2sInitialized) {
        i2s_driver_uninstall(I2S_PORT);
    }
}

bool TTS::initialize(const String& apiKey) {
    Serial.println("Initializing TTS...");
    
    // Store API key
    deepgramApiKey = apiKey;
    
    // Initialize I2S for audio output
    if (!initializeI2S()) {
        Serial.println("Failed to initialize I2S");
        return false;
    }
    
    Serial.println("TTS initialized successfully!");
    return true;
}

bool TTS::speakText(const String& text) {
    if (text.isEmpty()) {
        Serial.println("TTS: Empty text provided");
        return false;
    }
    
    // Try lazy initialization if not already initialized
    if (!i2sInitialized && !ensureInitialized()) {
        Serial.println("TTS: I2S not initialized and lazy initialization failed");
        return false;
    }
    
    Serial.printf("TTS: Speaking text: %s\n", text.c_str());
    
    // Use streaming approach to avoid memory allocation issues
    return streamDeepgramAPI(text);
}

bool TTS::streamDeepgramAPI(const String& text) {
    Serial.printf("🤖 Synthesizing with Deepgram TTS (streaming): \"%s\"\n", text.c_str());

    if (deepgramApiKey.length() < 10) {
        Serial.println("❌ Deepgram API key is not set or too short");
        return false;
    }

    String jsonPayload = "{\"text\":\"" + text + "\"}";
    Serial.println("TTS JSON Request:");
    Serial.println(jsonPayload);

    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();  // Skip SSL verification for simplicity

    http.begin(client, DEEPGRAM_URL);
    http.addHeader("Content-Type", "application/json");
    String authHeader = "Token " + deepgramApiKey;
    http.addHeader("Authorization", authHeader);
    http.setTimeout(180000);  // 3 minute timeout

    int httpCode = http.POST(jsonPayload);
    Serial.printf("Deepgram TTS HTTP Response Code: %d\n", httpCode);

    if (httpCode == HTTP_CODE_OK) {
        WiFiClient* stream = http.getStreamPtr();
        if (!stream) {
            Serial.println("❌ Failed to get stream pointer from HTTPClient");
            http.end();
            return false;
        }

        Serial.println("✅ Starting to stream and play audio data...");
        
        // Clear I2S DMA buffer before starting
        i2s_zero_dma_buffer(I2S_PORT);
        
        size_t totalBytesReceived = 0;
        size_t bytesWrittenToI2S = 0;
        unsigned long lastDataTime = millis();
        const unsigned long streamReadTimeout = 60000;  // 60 second timeout

        // Stream and play audio in real-time
        while (http.connected() && (stream->available() || (millis() - lastDataTime < streamReadTimeout))) {
            if (!http.connected()) {
                Serial.println("❌ HTTP connection lost during stream");
                break;
            }
            
            if (stream->available()) {
                size_t bytesRead = stream->readBytes(audioBuffer, BUFFER_SIZE);
                if (bytesRead > 0) {
                    totalBytesReceived += bytesRead;
                    
                    // Write directly to I2S - the audio is already in the right format
                    size_t bytesWritten;
                    esp_err_t err = i2s_write(I2S_PORT, audioBuffer, bytesRead, &bytesWritten, portMAX_DELAY);
                    if (err != ESP_OK) {
                        Serial.printf("❌ I2S write error: %s\n", esp_err_to_name(err));
                        break;
                    }
                    
                    if (bytesWritten < bytesRead) {
                        Serial.printf("⚠️ I2S underrun: tried to write %u, only wrote %u\n", bytesRead, bytesWritten);
                    }
                    
                    bytesWrittenToI2S += bytesWritten;
                    lastDataTime = millis();
                    
                    // Debug output every 8KB
                    if ((totalBytesReceived / 8192) != ((totalBytesReceived - bytesRead) / 8192)) {
                        Serial.printf("🔊 Streamed %u bytes so far...\n", totalBytesReceived);
                    }
                }
            } else {
                yield();
                delay(10);  // Small delay when no data available
            }
        }

        Serial.printf("✅ Finished streaming. Received: %u bytes, Sent to I2S: %u bytes\n", 
                     totalBytesReceived, bytesWrittenToI2S);
        
        http.end();
        
        if (totalBytesReceived > 0) {
            // Calculate approximate playback duration and wait
            unsigned long estimatedDurationMs = (bytesWrittenToI2S * 1000) / (SAMPLE_RATE * 2);  // 16-bit samples
            unsigned long waitTime = estimatedDurationMs + 500;  // Add 500ms buffer
            
            Serial.printf("Waiting %lu ms for audio playback to complete...\n", waitTime);
            delay(waitTime);
            
            // Clear DMA buffer after playback
            i2s_zero_dma_buffer(I2S_PORT);
        }
        
        return totalBytesReceived > 0;
    } else {
        Serial.printf("❌ Deepgram TTS request failed. HTTP Code: %d\n", httpCode);
        String errorPayload = http.getString();
        if (errorPayload.length() > 0) {
            Serial.println("Error payload:");
            Serial.println(errorPayload);
        }
        http.end();
        return false;
    }
}

bool TTS::ensureInitialized() {
    if (i2sInitialized) {
        return true;
    }
    
    if (deepgramApiKey.isEmpty()) {
        Serial.println("TTS: No API key set for lazy initialization");
        return false;
    }
    
    Serial.println("TTS: Attempting lazy initialization...");
    return initializeI2S();
}

bool TTS::initializeI2S() {
    Serial.println("Initializing I2S for MAX98357A");
    
    // First, make sure the I2S port is not already in use
    i2s_driver_uninstall(I2S_PORT);
    
    // I2S configuration matching the working example
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,  // 48000 Hz
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,  // Mono right channel for MAX98357A
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 1024,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };
    
    // I2S pin configuration matching working example
    i2s_pin_config_t pin_config = {
        .bck_io_num = BCLK_PIN,     // 26
        .ws_io_num = LRCLK_PIN,     // 25  
        .data_out_num = DATA_PIN,   // 22
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    
    Serial.println("Installing I2S driver...");
    esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("Failed to install I2S driver: %s\n", esp_err_to_name(err));
        return false;
    }
    
    err = i2s_set_pin(I2S_PORT, &pin_config);
    if (err != ESP_OK) {
        Serial.printf("Failed to set I2S pins: %s\n", esp_err_to_name(err));
        i2s_driver_uninstall(I2S_PORT);
        return false;
    }
    
    i2sInitialized = true;
    Serial.println("I2S initialized successfully!");
    
    // Clear DMA buffer to ensure clean start
    i2s_zero_dma_buffer(I2S_PORT);
    
    return true;
}

bool TTS::callDeepgramAPI(const String& text, uint8_t** audioData, size_t* dataSize) {
    Serial.printf("🤖 Calling Deepgram TTS API: \"%s\"\n", text.c_str());

    if (deepgramApiKey.length() < 10) {
        Serial.println("❌ Deepgram API key is not set or too short");
        return false;
    }

    String jsonPayload = "{\"text\":\"" + text + "\"}";
    
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();  // Skip SSL verification for simplicity
    
    http.begin(client, DEEPGRAM_URL);
    http.addHeader("Content-Type", "application/json");
    String authHeader = "Token " + deepgramApiKey;
    http.addHeader("Authorization", authHeader);
    http.setTimeout(180000);  // 3 minute timeout

    int httpCode = http.POST(jsonPayload);
    Serial.printf("Deepgram TTS HTTP Response Code: %d\n", httpCode);

    if (httpCode == HTTP_CODE_OK) {
        WiFiClient* stream = http.getStreamPtr();
        if (!stream) {
            Serial.println("❌ Failed to get stream pointer");
            http.end();
            return false;
        }

        // Get content length if available
        int contentLength = http.getSize();
        Serial.printf("Content length: %d bytes\n", contentLength);

        // Allocate initial buffer
        size_t bufferSize = (contentLength > 0) ? contentLength : 8192;  // Default 8KB if unknown
        *audioData = (uint8_t*)malloc(bufferSize);
        if (*audioData == nullptr) {
            Serial.println("❌ Failed to allocate memory for audio data");
            http.end();
            return false;
        }

        size_t totalRead = 0;
        unsigned long lastDataTime = millis();
        const unsigned long timeout = 60000;  // 60 second timeout

        // Read all data from stream
        while (http.connected() && (stream->available() || (millis() - lastDataTime < timeout))) {
            if (stream->available()) {
                // Resize buffer if needed
                if (totalRead + 1024 > bufferSize) {
                    bufferSize += 4096;
                    uint8_t* newBuffer = (uint8_t*)realloc(*audioData, bufferSize);
                    if (newBuffer == nullptr) {
                        Serial.println("❌ Failed to resize audio buffer");
                        free(*audioData);
                        *audioData = nullptr;
                        http.end();
                        return false;
                    }
                    *audioData = newBuffer;
                }

                size_t bytesRead = stream->readBytes(*audioData + totalRead, min(1024, (int)(bufferSize - totalRead)));
                if (bytesRead > 0) {
                    totalRead += bytesRead;
                    lastDataTime = millis();
                }
            } else {
                yield();
                delay(10);
            }
        }

        *dataSize = totalRead;
        Serial.printf("✅ Successfully received %u bytes of audio data\n", totalRead);
        
        http.end();
        return totalRead > 0;
    } else {
        Serial.printf("❌ HTTP request failed with code: %d\n", httpCode);
        String response = http.getString();
        if (response.length() > 0) {
            Serial.println("Error response:");
            Serial.println(response);
        }
        http.end();
        return false;
    }
}

bool TTS::playAudioData(const uint8_t* audioData, size_t dataSize) {
    if (!i2sInitialized) {
        Serial.println("TTS: I2S not initialized");
        return false;
    }
    
    if (audioData == nullptr || dataSize == 0) {
        Serial.println("TTS: Invalid audio data");
        return false;
    }
    
    Serial.printf("▶️ Playing RAW audio data: %u bytes\n", dataSize);
    
    // Clear DMA buffer before starting playback
    i2s_zero_dma_buffer(I2S_PORT);
    
    size_t totalWritten = 0;
    size_t bytesWritten;
    
    // Write raw PCM data directly to I2S in chunks
    while (totalWritten < dataSize) {
        size_t chunkSize = min(BUFFER_SIZE, dataSize - totalWritten);
        
        esp_err_t err = i2s_write(I2S_PORT, audioData + totalWritten, chunkSize, &bytesWritten, portMAX_DELAY);
        if (err != ESP_OK) {
            Serial.printf("❌ I2S write error: %s\n", esp_err_to_name(err));
            break;
        }
        
        if (bytesWritten < chunkSize) {
            Serial.printf("⚠️ I2S underrun: tried to write %u, only wrote %u\n", chunkSize, bytesWritten);
        }
        
        totalWritten += bytesWritten;
        yield();  // Allow other tasks to run
    }
    
    Serial.printf("🎵 Finished playing audio. Total bytes sent to I2S: %u\n", totalWritten);
    
    if (totalWritten > 0) {
        // Calculate playback duration and wait for completion
        unsigned long estimatedDurationMs = (totalWritten * 1000) / (SAMPLE_RATE * 2);  // 16-bit samples
        unsigned long waitTime = estimatedDurationMs + 100;  // Add small buffer
        
        Serial.printf("Waiting %lu ms for audio playback to complete...\n", waitTime);
        delay(waitTime);
        
        // Clear DMA buffer after playback
        i2s_zero_dma_buffer(I2S_PORT);
    }
    
    return totalWritten == dataSize;
}

void TTS::stopPlayback() {
    // For simple implementation, we just stop feeding data to I2S
    // The MAX98357A will naturally stop when no more data is provided
    Serial.println("TTS: Stopping playback");
}

void TTS::setVolume(float volume) {
    // MAX98357A doesn't have software volume control
    // Volume is controlled by the GAIN pin (hardware)
    // For software volume control, you'd need to scale the audio samples
    Serial.printf("TTS: Volume control not implemented (MAX98357A uses hardware gain)\n");
    Serial.printf("TTS: Requested volume: %.2f\n", volume);
}

void TTS::cleanupAudioData(uint8_t* audioData) {
    if (audioData != nullptr) {
        free(audioData);
    }
}
