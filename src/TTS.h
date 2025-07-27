#ifndef TTS_H
#define TTS_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "driver/i2s.h"

class TTS {
private:
    // I2S pins for MAX98357A
    static const int BCLK_PIN = 14;
    static const int LRCLK_PIN = 13;
    static const int DATA_PIN = 2;
    static const int GAIN_PIN = 21;  // Hardware gain control pin for MAX98357A
    
    // Audio configuration (matching working example)
    static const int SAMPLE_RATE = 16000;  // 16kHz as used in working example
    static const int BITS_PER_SAMPLE = 16;
    static const i2s_port_t I2S_PORT = I2S_NUM_1;
    
    // API configuration (matching working example)
    static const char* DEEPGRAM_URL;
    
    bool i2sInitialized;
    String deepgramApiKey;
    
    // Audio gain control
    float softwareGain;  // Software gain multiplier (0.0 to 2.0)
    bool hardwareGainHigh;  // Hardware gain setting (true = +9dB, false = +6dB)
    
    // Buffer for audio data (increased for better streaming)
    static const size_t BUFFER_SIZE = 16384;  // Larger buffer for better streaming
    uint8_t audioBuffer[BUFFER_SIZE];
    
public:
    TTS();
    ~TTS();
    
    // Initialization
    bool initialize(const String& apiKey);
    
    // Main TTS function
    bool speakText(const String& text);
    
    // Lazy initialization - try to initialize if not already done
    bool ensureInitialized();
    
    // Audio playback control
    bool playAudioData(const uint8_t* audioData, size_t dataSize);
    void stopPlayback();
    
    // Configuration
    void setVolume(float volume); // 0.0 to 1.0
    void setSoftwareGain(float gain); // 0.0 to 2.0 (software amplification)
    void setHardwareGain(bool highGain); // true = +9dB, false = +6dB
    float getSoftwareGain() const;
    bool getHardwareGain() const;
    
    // WiFi optimization for maximum speed
    static void optimizeWiFiForSpeed();
    
private:
    // Internal methods
    bool initializeI2S();
    bool callDeepgramAPI(const String& text, uint8_t** audioData, size_t* dataSize);
    bool streamDeepgramAPI(const String& text);  // Streaming method for raw PCM
    void cleanupAudioData(uint8_t* audioData);
    void applySoftwareGain(uint8_t* audioData, size_t dataSize);  // Apply software gain to audio data
    void initializeGainPin();  // Initialize hardware gain control pin
    
    // Static callback for HTTP response (if needed for future use)
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
};

// Structure to hold response data
struct ResponseData {
    uint8_t* data;
    size_t size;
    size_t capacity;
};

#endif // TTS_H
