#include "deepgram_client.h"
#include "secrets.h"

#include "esp_heap_caps.h"

DeepgramClient::DeepgramClient(const char* api_key) : api_key(api_key), defaultLanguage("en-US"), stt_doc(nullptr), response_buffer(nullptr) {
    // Constructor now only initializes variables, allocation is handled in begin()
}

DeepgramClient::~DeepgramClient() {
    // Free memory
    if (stt_doc) {
        delete stt_doc;
    }
    if (response_buffer) {
        free(response_buffer);
    }
}

bool DeepgramClient::begin() {
    // Allocate memory in PSRAM
    stt_doc = new DynamicJsonDocument(STT_DOC_SIZE);
    response_buffer = (char*)ps_malloc(RESPONSE_BUFFER_SIZE);

    if (!stt_doc || !response_buffer) {
        Serial.println("FATAL: Failed to allocate memory for DeepgramClient in PSRAM!");
        return false;
    }
    return true;
}

uint8_t* DeepgramClient::createWAVData(const uint8_t* pcm_data, size_t pcm_size, size_t* wav_size) {
    // Calculate WAV file size
    size_t wav_data_size = sizeof(WAVHeader) + pcm_size;
    *wav_size = wav_data_size;
    
    // Allocate memory for WAV data
    uint8_t* wav_data = (uint8_t*)ps_malloc(wav_data_size);
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
    if (response.isEmpty() || !stt_doc) {
        return "";
    }
    
    // Use a larger document size to handle the deeply nested Deepgram response
    stt_doc->clear();
    
    // Increase capacity for the complex Deepgram response structure
    DeserializationError error = deserializeJson(*stt_doc, response);
    
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
    if (stt_doc->containsKey("results") && (*stt_doc)["results"].containsKey("channels") &&
        (*stt_doc)["results"]["channels"].size() > 0) {
        JsonArray alternatives = (*stt_doc)["results"]["channels"][0]["alternatives"];
        if (alternatives.size() > 0) {
            String transcript = alternatives[0]["transcript"].as<String>();
            return transcript;
        }
    }
    
    // If extraction failed, return empty string
    Serial.println("Could not extract transcript from response");
    return "";
}

bool DeepgramClient::extractSearchResults(const String& response, const String& searchTerm, float minConfidence) {
    if (response.isEmpty() || !stt_doc) {
        return false;
    }
    
    stt_doc->clear();
    DeserializationError error = deserializeJson(*stt_doc, response);
    
    if (error) {
        Serial.printf("JSON parsing error for search results: %s\n", error.c_str());
        return false;
    }
    
    // Extract search results from Deepgram response format
    if (stt_doc->containsKey("results") && (*stt_doc)["results"].containsKey("channels") &&
        (*stt_doc)["results"]["channels"].size() > 0) {
        
        JsonArray searches = (*stt_doc)["results"]["channels"][0]["search"];
        
        for (JsonObject search : searches) {
            String query = search["query"].as<String>();
            
            // Check if this search matches our search term (case insensitive)
            if (query.equalsIgnoreCase(searchTerm)) {
                JsonArray hits = search["hits"];
                
                for (JsonObject hit : hits) {
                    float confidence = hit["confidence"].as<float>();
                    float start = hit["start"].as<float>();
                    float end = hit["end"].as<float>();
                    String snippet = hit["snippet"].as<String>();
                    
                    Serial.printf("🔍 Search hit for '%s': confidence=%.3f, time=%.1f-%.1fs, snippet='%s'\n", 
                                 query.c_str(), confidence, start, end, snippet.c_str());
                    
                    if (confidence >= minConfidence) {
                        Serial.printf("✅ Wake word '%s' detected with confidence %.3f (threshold: %.3f)\n", 
                                     query.c_str(), confidence, minConfidence);
                        return true;
                    }
                }
            }
        }
    }
    
    return false;
}

String DeepgramClient::transcribe(const uint8_t* audio_data, size_t data_size) {
    return transcribe(audio_data, data_size, defaultLanguage);
}

String DeepgramClient::transcribe(const uint8_t* audio_data, size_t data_size, const String& language) {
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
    
    Serial.printf("Sending %d bytes of WAV data to Deepgram (PCM: %d bytes, checksum: %08X, language: %s)\n", 
                  wav_size, data_size, audio_checksum, language.c_str());
    
    // Build URL with language parameter
    String deepgramUrl = "https://api.deepgram.com/v1/listen?model=nova-2&smart_format=true";
    if (!language.isEmpty() && language != "en-US") {
        deepgramUrl += "&language=" + language;
    }
    
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();
    if (http.begin(client, deepgramUrl)) {
        http.addHeader("Authorization", "Token " + String(DEEPGRAM_API_KEY));
        http.addHeader("Content-Type", "audio/wav");
        
        // Set timeout for large audio files
        http.setTimeout(10000); // 10 seconds

        int httpCode = http.POST(wav_data, wav_size);

        if (httpCode > 0) {
            if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
                int len = http.getSize();
                WiFiClient* stream = http.getStreamPtr();
                if (response_buffer) {
                    int read = stream->readBytes(response_buffer, min(len, (int)RESPONSE_BUFFER_SIZE - 1));
                    response_buffer[read] = '\0';
                    Serial.println("✅ Deepgram transcription successful");
                } else {
                    Serial.println("❌ Response buffer not allocated!");
                }
                
                // Extract transcript from JSON response
                String transcript = extractTranscript(response_buffer);
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

void DeepgramClient::setDefaultLanguage(const String& language) {
    defaultLanguage = language;
    Serial.printf("DeepgramClient default language set to: %s\n", language.c_str());
}

bool DeepgramClient::searchForWakeWords(const uint8_t* audio_data, size_t data_size, const char* wakeWords[], int wakeWordCount, float minConfidence) {
    // Validate input data
    if (!audio_data || data_size == 0 || !wakeWords || wakeWordCount == 0) {
        Serial.println("Invalid parameters for wake word search");
        return false;
    }
    
    if (data_size < 1000) { // Less than ~30ms of audio at 16kHz 16-bit
        Serial.printf("Audio data too small for wake word search: %d bytes\n", data_size);
        return false;
    }

    // Convert raw PCM to WAV format
    size_t wav_size;
    uint8_t* wav_data = createWAVData(audio_data, data_size, &wav_size);
    if (!wav_data) {
        Serial.println("Failed to create WAV data for wake word search");
        return false;
    }
    
    // Build URL with search parameters for wake words
    String deepgramUrl = "https://api.deepgram.com/v1/listen?model=nova-2";
    
    // Add search parameters for each wake word
    for (int i = 0; i < wakeWordCount; i++) {
        String wakeWord = String(wakeWords[i]);
        // URL encode spaces and special characters
        wakeWord.replace(" ", "%20");
        deepgramUrl += "&search=" + wakeWord;
    }
    
    // Add language if not default
    if (!defaultLanguage.isEmpty() && defaultLanguage != "en-US") {
        deepgramUrl += "&language=" + defaultLanguage;
    }
    
    Serial.printf("🔍 Searching for wake words in %d bytes of audio...\n", data_size);
    Serial.printf("URL: %s\n", deepgramUrl.c_str());
    
    bool wakeWordFound = false;
    
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();
    if (http.begin(client, deepgramUrl)) {
        http.addHeader("Authorization", "Token " + String(DEEPGRAM_API_KEY));
        http.addHeader("Content-Type", "audio/wav");
        
        // Set timeout for audio processing
        http.setTimeout(10000); // 10 seconds

        int httpCode = http.POST(wav_data, wav_size);

        if (httpCode > 0) {
            if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
                int len = http.getSize();
                WiFiClient* stream = http.getStreamPtr();
                if (response_buffer) {
                    int read = stream->readBytes(response_buffer, min(len, (int)RESPONSE_BUFFER_SIZE - 1));
                    response_buffer[read] = '\0';
                    Serial.println("✅ Deepgram search request successful");
                } else {
                    Serial.println("❌ Response buffer not allocated!");
                }
                
                // Check each wake word for matches
                for (int i = 0; i < wakeWordCount && !wakeWordFound; i++) {
                    if (extractSearchResults(response_buffer, String(wakeWords[i]), minConfidence)) {
                        wakeWordFound = true;
                        break;
                    }
                }
                
                if (!wakeWordFound) {
                    Serial.println("🔍 No wake words found above confidence threshold");
                }
            } else {
                Serial.printf("❌ HTTP Error Code: %d\n", httpCode);
                String error_response = http.getString();
                Serial.println("Error Response: " + error_response);
            }
        } else {
            Serial.printf("❌ [HTTP] Wake word search failed, error: %s\n", http.errorToString(httpCode).c_str());
        }
        http.end();
    } else {
        Serial.printf("❌ [HTTP] Unable to connect to Deepgram for wake word search\n");
    }
    
    // Free the WAV data
    free(wav_data);

    return wakeWordFound;
}
