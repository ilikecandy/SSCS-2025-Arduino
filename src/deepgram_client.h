#ifndef DEEPGRAM_CLIENT_H
#define DEEPGRAM_CLIENT_H

#include <HTTPClient.h>
#include <ArduinoJson.h>

struct WAVHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t chunk_size;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt[4] = {'f', 'm', 't', ' '};
    uint32_t fmt_chunk_size = 16;
    uint16_t audio_format = 1; // PCM
    uint16_t num_channels = 1;
    uint32_t sample_rate = 16000;
    uint32_t byte_rate = 32000; // sample_rate * num_channels * bits_per_sample / 8
    uint16_t block_align = 2; // num_channels * bits_per_sample / 8
    uint16_t bits_per_sample = 16;
    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t data_size;
};

class DeepgramClient {
private:
    HTTPClient http;
    const char* api_key;
    String defaultLanguage;
    
    // Helper function to create WAV data from raw PCM
    uint8_t* createWAVData(const uint8_t* pcm_data, size_t pcm_size, size_t* wav_size);
    
    // Helper function to extract transcript from Deepgram response
    String extractTranscript(const String& response);
    
    // Helper function to extract search results from Deepgram response
    bool extractSearchResults(const String& response, const String& searchTerm, float minConfidence = 0.5);

public:
    DeepgramClient(const char* api_key);
    String transcribe(const uint8_t* audio_data, size_t data_size);
    String transcribe(const uint8_t* audio_data, size_t data_size, const String& language);
    
    // Search for specific terms/phrases in audio (for wake word detection)
    bool searchForWakeWords(const uint8_t* audio_data, size_t data_size, const char* wakeWords[], int wakeWordCount, float minConfidence = 0.5);
    
    // Set default language for transcription
    void setDefaultLanguage(const String& language);
};

#endif