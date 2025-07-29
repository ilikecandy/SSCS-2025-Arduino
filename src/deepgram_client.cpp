#include "deepgram_client.h"
#include "secrets.h"

DeepgramClient::DeepgramClient(const char* api_key) : api_key(api_key) {}

uint8_t* DeepgramClient::createWAVData(const uint8_t* pcm_data, size_t pcm_size, size_t* wav_size) {
    // Calculate WAV file size
    size_t wav_data_size = sizeof(WAVHeader) + pcm_size;
    *wav_size = wav_data_size;
    
    // Allocate memory for WAV data
    uint8_t* wav_data = (uint8_t*)malloc(wav_data_size);
    if (!wav_data) {
        Serial.println("Failed to allocate memory for WAV data");
        return nullptr;
    }
    
    // Create WAV header
    WAVHeader header;
    header.chunk_size = wav_data_size - 8;
    header.data_size = pcm_size;
    header.sample_rate = 16000;
    header.byte_rate = 16000 * 2;
    header.block_align = 2;
    
    // Copy header and PCM data
    memcpy(wav_data, &header, sizeof(WAVHeader));
    memcpy(wav_data + sizeof(WAVHeader), pcm_data, pcm_size);
    
    // Basic audio quality check - look for silence or clipping
    int16_t* samples = (int16_t*)pcm_data;
    int sample_count = pcm_size / 2;
    int silent_samples = 0;
    int clipped_samples = 0;
    
    for (int i = 0; i < sample_count; i++) {
        int16_t sample = samples[i];
        if (abs(sample) < 100) { // Very quiet
            silent_samples++;
        }
        if (abs(sample) > 30000) { // Near clipping
            clipped_samples++;
        }
    }
    
    float silence_percent = (float)silent_samples / sample_count * 100.0f;
    float clipping_percent = (float)clipped_samples / sample_count * 100.0f;
    
    Serial.printf("Audio quality: %.1f%% silent, %.1f%% clipped, %d samples\n", 
                  silence_percent, clipping_percent, sample_count);
    
    return wav_data;
}

String DeepgramClient::extractTranscript(const String& response) {
    if (response.isEmpty()) {
        return "";
    }
    
    // Use a larger document size to handle the deeply nested Deepgram response
    JsonDocument doc;
    
    // Increase capacity for the complex Deepgram response structure
    DeserializationError error = deserializeJson(doc, response);
    
    if (error) {
        Serial.printf("  %s\n", error.c_str());
        
        // Try manual extraction as fallback
        int transcriptStart = response.indexOf("\"transcript\":\"");
        if (transcriptStart != -1) {
            transcriptStart += 14; // Length of "\"transcript\":\""
            int transcriptEnd = response.indexOf("\"", transcriptStart);
            if (transcriptEnd != -1) {
                String transcript = response.substring(transcriptStart, transcriptEnd);
                Serial.println("Manual extraction successful: " + transcript);
                return transcript;
            }
        }
        
        Serial.println("Failed to extract transcript manually");
        return "";
    }
    
    // Extract transcript from Deepgram response format
    if (doc.containsKey("results") && doc["results"].containsKey("channels") && 
        doc["results"]["channels"].size() > 0) {
        JsonArray alternatives = doc["results"]["channels"][0]["alternatives"];
        if (alternatives.size() > 0) {
            String transcript = alternatives[0]["transcript"].as<String>();
            return transcript;
        }
    }
    
    // If extraction failed, return empty string
    Serial.println("Could not extract transcript from response");
    return "";
}

String DeepgramClient::transcribe(const uint8_t* audio_data, size_t data_size) {
    String response = "";
    
    // Validate input data
    if (!audio_data || data_size == 0) {
        Serial.println("Invalid audio data provided to transcribe");
        return response;
    }
    
    if (data_size < 1000) { // Less than ~30ms of audio at 16kHz 16-bit
        Serial.printf("Audio data too small: %d bytes\n", data_size);
        return response;
    }

    // Convert raw PCM to WAV format
    size_t wav_size;
    uint8_t* wav_data = createWAVData(audio_data, data_size, &wav_size);
    if (!wav_data) {
        Serial.println("Failed to create WAV data");
        return response;
    }
    
    // Calculate a simple checksum to track unique audio samples
    uint32_t audio_checksum = 0;
    for (size_t i = 0; i < min(data_size, (size_t)1000); i += 4) {
        audio_checksum ^= *(uint32_t*)(audio_data + i);
    }
    
    Serial.printf("Sending %d bytes of WAV data to Deepgram (PCM: %d bytes, checksum: %08X)\n", 
                  wav_size, data_size, audio_checksum);
    
    if (http.begin("https://api.deepgram.com/v1/listen?model=nova-2&smart_format=true")) {
        http.addHeader("Authorization", "Token " + String(DEEPGRAM_API_KEY));
        http.addHeader("Content-Type", "audio/wav");
        
        // Set timeout for large audio files
        http.setTimeout(10000); // 10 seconds

        int httpCode = http.POST(wav_data, wav_size);

        if (httpCode > 0) {
            if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
                String raw_response = http.getString();
                Serial.println("✅ Deepgram transcription successful");
                
                // Extract transcript from JSON response
                String transcript = extractTranscript(raw_response);
                if (!transcript.isEmpty()) {
                    response = transcript;
                } else {
                    Serial.println("No transcript found in response");
                }
            } else {
                Serial.printf("❌ HTTP Error Code: %d\n", httpCode);
                String error_response = http.getString();
                Serial.println("Error Response: " + error_response);
                Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
            }
        } else {
            Serial.printf("❌ [HTTP] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
        }
        http.end();
    } else {
        Serial.printf("❌ [HTTP] Unable to connect to Deepgram\n");
    }
    
    // Free the WAV data
    free(wav_data);

    return response;
}