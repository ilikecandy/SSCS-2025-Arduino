#ifndef TTS_H
#define TTS_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "driver/i2s.h"
#include "i2s_manager.h"

class TTS {
private:
    // I2S pins for MAX98357A (Speaker uses pin 12 for clock, mic uses pin 14)
    static const int BCLK_PIN = 12;  // Changed from 14 to 12 to match I2S_SPEAKER_SERIAL_CLOCK
    static const int LRCLK_PIN = 13;
    static const int DATA_PIN = 2;
    
    // Audio configuration (matching working example)
    static const int SAMPLE_RATE = 16000;  // 16kHz as used in working example
    static const int BITS_PER_SAMPLE = 16;
    static const i2s_port_t I2S_PORT = I2S_NUM_1;
    
    // API configuration (matching working example)
    static const char* DEEPGRAM_URL;
    
    bool i2sInitialized;
    String deepgramApiKey;
    String defaultLanguage;
    
    // Audio gain control
    float softwareGain;  // Software gain multiplier (0.0 to 2.0)
    
    // Buffer for audio data (increased for better streaming)
    static const size_t BUFFER_SIZE = 16384;  // Larger buffer for better streaming
    uint8_t* audioBuffer;
    
    // Shared WiFi client instance for all HTTPS requests
    WiFiClientSecure* wifiClient;
    bool wifiClientInitialized;
    
public:
    TTS();
    ~TTS();
    
    // Initialization
    bool initialize(const String& apiKey);
    
    // Main TTS function
    bool speakText(const String& text);
    bool speakText(const String& text, const String& language);
    
    // Lazy initialization - try to initialize if not already done
    bool ensureInitialized();
    
    // Language configuration
    void setDefaultLanguage(const String& language);
    
    // Audio playback control
    bool playAudioData(const uint8_t* audioData, size_t dataSize);
    void stopPlayback();
    
    // I2S resource management
    bool requestSpeakerAccess();
    void releaseSpeakerAccess();
    
    // Configuration
    void setVolume(float volume); // 0.0 to 1.0
    void setSoftwareGain(float gain); // 0.0 to 2.0 (software amplification)
    float getSoftwareGain() const;
    
    // WiFi optimization for maximum speed
    static void optimizeWiFiForSpeed();
    
private:
    // Internal methods
    bool initializeI2S();
    bool callDeepgramAPI(const String& text, uint8_t** audioData, size_t* dataSize);
    bool callDeepgramAPI(const String& text, const String& language, uint8_t** audioData, size_t* dataSize);
    bool streamDeepgramAPI(const String& text);  // Streaming method for raw PCM
    bool streamDeepgramAPI(const String& text, const String& language);  // Streaming method with language
    void cleanupAudioData(uint8_t* audioData);
    void applySoftwareGain(uint8_t* audioData, size_t dataSize);  // Apply software gain to audio data
    
    // WiFi client management
    WiFiClientSecure* getSharedWiFiClient();
    bool initializeWiFiClient();
    
    // Static callback for HTTP response (if needed for future use)
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
};

// Structure to hold response data
struct ResponseData {
    uint8_t* data;
    size_t size;
    size_t capacity;
};

#endif
