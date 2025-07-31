#include <Arduino.h>
#include <math.h>
#include <HTTPClient.h>
#include "vision_assistant.h"
#include "TTS.h"
#include "secrets.h"
#include "microphone.h"
#include "deepgram_client.h"
#include "settings_manager.h"
#include <ArduinoJson.h>

VisionAssistant visionAssistant;
TTS tts;
DeepgramClient deepgramClient(DEEPGRAM_API_KEY);
SettingsManager settingsManager(NOTIFICATIONS_API_URL);
bool ttsAvailable = false;

// Wake words (optimized for Deepgram's acoustic search - longer phrases work better)
const char* WAKE_WORDS[] = {
    "halo",
};
const int WAKE_WORDS_COUNT = sizeof(WAKE_WORDS) / sizeof(WAKE_WORDS[0]);

// Audio buffer
const int AUDIO_BUFFER_SECONDS = 3;
const int SAMPLE_RATE = 16000;
const int BITS_PER_SAMPLE = 16;
const int CHANNELS = 1;
const int AUDIO_BUFFER_SIZE = AUDIO_BUFFER_SECONDS * SAMPLE_RATE * (BITS_PER_SAMPLE / 8) * CHANNELS;
uint8_t* audio_buffer = nullptr;
volatile int audio_buffer_index = 0;  // Made volatile for dual-core access
volatile bool is_recording = false;   // Made volatile for dual-core access
volatile bool buffer_has_wrapped = false;  // Track if buffer has wrapped around
volatile uint32_t buffer_sequence = 0;  // Sequence number to track buffer updates

// Core synchronization
TaskHandle_t AudioTaskHandle = NULL;
SemaphoreHandle_t audioMutex;
QueueHandle_t commandQueue;
QueueHandle_t audioCommandQueue; // For sending commands to the audio task

// Enum for audio task commands
enum class AudioCommandType {
    SPEAK_TEXT,
    PLAY_DING
};

// Struct for audio commands
struct AudioCommand {
    AudioCommandType type;
    char text[256]; // For SPEAK_TEXT command
};

// Function declarations
void audioTask(void *pvParameters);
void process_audio();
void playDingSound();
String cleanTextForWakeWord(const String& text);
void sendEmergencyAlert(const String& alertType, const String& description);
void handleSystemAction(const JsonDocument& doc);
void initializeLanguageSettings();

// Tool call handler for system actions
void toolHandler(const String& toolName, const String& jsonParams) {
    Serial.printf("Tool call handler invoked for: %s\n", toolName.c_str());
    if (toolName == "systemAction") {
        Serial.printf("systemAction call received with params: %s\n", jsonParams.c_str());
        
        // Parse JSON parameters
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, jsonParams);
        
        if (error) {
            Serial.printf("Failed to parse JSON parameters: %s\n", error.c_str());
            return;
        }
        
        // Handle the systemAction
        handleSystemAction(doc);
    }
}

void playDingSound() {
    Serial.println("🔔 Playing wake word confirmation ding...");
    
    // Generate a pleasant ding sound (two-tone chime)
    const int dingDuration = 300;  // Total duration in ms
    const int sampleRate = 16000;
    const int samplesPerTone = (sampleRate * dingDuration / 2) / 1000;  // Split into two tones
    const int totalSamples = samplesPerTone * 2;
    
    // Allocate buffer for ding sound
    uint8_t* dingBuffer = (uint8_t*)malloc(totalSamples * 2);  // 16-bit samples
    if (!dingBuffer) {
        Serial.println("❌ Failed to allocate ding buffer");
        return;
    }
    
    int16_t* samples = (int16_t*)dingBuffer;
    
    // Generate first tone (800 Hz)
    for (int i = 0; i < samplesPerTone; i++) {
        float t = (float)i / sampleRate;
        float amplitude = 0.3f * (1.0f - t * 2);  // Fade out
        samples[i] = (int16_t)(amplitude * 8000 * sin(2 * PI * 800 * t));
    }
    
    // Generate second tone (1000 Hz)
    for (int i = 0; i < samplesPerTone; i++) {
        float t = (float)i / sampleRate;
        float amplitude = 0.3f * (1.0f - t * 2);  // Fade out
        samples[samplesPerTone + i] = (int16_t)(amplitude * 8000 * sin(2 * PI * 1000 * t));
    }
    
    // Use TTS to play the ding sound (TTS handles I2S management properly)
    bool dingSuccess = false;
    if (ttsAvailable) {
        dingSuccess = tts.playAudioData(dingBuffer, totalSamples * 2);
        if (!dingSuccess) {
            Serial.println("❌ Failed to play ding sound via TTS");
        }
    } else {
        Serial.println("❌ TTS not available for ding sound");
    }
    
    // Clean up
    free(dingBuffer);
    
    if (dingSuccess) {
        Serial.println("🔔 Ding sound complete - ready for command!");
    } else {
        Serial.println("🔔 Ding sound failed but continuing - ready for command!");
    }
}

void sendEmergencyAlert(const String& alertType, const String& description) {
    Serial.println("🚨 Emergency protocol activated!");
    Serial.printf("Alert Type: %s\n", alertType.c_str());
    Serial.printf("Description: %s\n", description.c_str());
    
    // Get current GPS location if available
    GPSData gpsData = visionAssistant.getCurrentGPSData();
    String locationInfo = "Location unknown";
    
    if (gpsData.isValid) {
        locationInfo = String("Lat: ") + String(gpsData.latitude, 6) + 
                      ", Lon: " + String(gpsData.longitude, 6) + 
                      ", Alt: " + String(gpsData.altitude, 1) + "m";
    }
    
    // Create emergency alert JSON
    JsonDocument alertDoc;
    alertDoc["type"] = "emergency_alert";
    alertDoc["timestamp"] = millis();
    alertDoc["alert_type"] = alertType;
    alertDoc["description"] = description;
    alertDoc["location"] = locationInfo;
    alertDoc["device_id"] = "SSCS-2025-Device";
    alertDoc["user_status"] = "emergency_detected";
    
    // Convert to string
    String jsonString;
    serializeJson(alertDoc, jsonString);
    
    // Send POST request to emergency API
    HTTPClient http;
    http.begin(String(NOTIFICATIONS_API_URL) + "/upload");
    http.addHeader("Content-Type", "application/json");
    
    Serial.println("📡 Sending emergency alert to API...");
    Serial.printf("URL: %s\n", String(NOTIFICATIONS_API_URL) + "/upload");
    Serial.printf("JSON: %s\n", jsonString.c_str());
    
    int httpResponseCode = http.POST(jsonString);
    
    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.printf("✅ Emergency alert sent successfully! Response code: %d\n", httpResponseCode);
        Serial.printf("Response: %s\n", response.c_str());
    } else {
        Serial.printf("❌ Failed to send emergency alert. Error code: %d\n", httpResponseCode);
        Serial.printf("Error: %s\n", http.errorToString(httpResponseCode).c_str());
    }
    
    http.end();
}

void handleSystemAction(const JsonDocument& doc) {
    // Extract parameters
    String intent = doc["intent"].as<String>();
    bool shouldSpeak = doc["shouldSpeak"].as<bool>();
    String message = doc["message"].as<String>();
    String logEntry = doc["logEntry"].as<String>();
    String routeTo = doc["routeTo"].as<String>();
    String routeParams = doc["routeParams"].as<String>();
    
    Serial.printf("Intent: %s\n", intent.c_str());
    Serial.printf("Should Speak: %s\n", shouldSpeak ? "true" : "false");
    Serial.printf("Message: %s\n", message.c_str());
    Serial.printf("Log Entry: %s\n", logEntry.c_str());
    Serial.printf("Route To: %s\n", routeTo.c_str());
    Serial.printf("Route Params: %s\n", routeParams.c_str());
    
    // Handle speaking if required
    if (shouldSpeak && !message.isEmpty()) {
        if (ttsAvailable) {
            AudioCommand cmd;
            cmd.type = AudioCommandType::SPEAK_TEXT;
            strncpy(cmd.text, message.c_str(), sizeof(cmd.text) - 1);
            cmd.text[sizeof(cmd.text) - 1] = '\0'; // Ensure null termination
            
            if (xQueueSend(audioCommandQueue, &cmd, 0) != pdTRUE) {
                Serial.println("❌ Failed to queue SPEAK_TEXT command");
            }
        } else {
            Serial.println("TTS not available to speak message.");
        }
    }
    
    // Log the entry if provided
    if (!logEntry.isEmpty()) {
        Serial.printf("LOG: %s\n", logEntry.c_str());
    }
    
    // Handle specific intents
    if (intent == "emergency_protocol") {
        Serial.println("🚨 Emergency protocol detected!");
        String alertType = "fall_detection"; // Default type
        String description = message.isEmpty() ? "Emergency detected by vision assistant" : message;
        
        // Send emergency alert
        sendEmergencyAlert(alertType, description);
    }
    else if (intent == "obstacle_alert") {
        Serial.println("⚠️ Obstacle alert detected!");
        // Log obstacle information
        if (!logEntry.isEmpty()) {
            Serial.printf("OBSTACLE LOG: %s\n", logEntry.c_str());
        }
        // Obstacle alerts should always be spoken for safety
        if (!message.isEmpty() && ttsAvailable) {
            tts.speakText(message);
        }
    }
    else if (intent == "contextual_assistance") {
        Serial.println("🗺️ Contextual assistance provided");
        // Log context information
        if (!logEntry.isEmpty()) {
            Serial.printf("CONTEXT LOG: %s\n", logEntry.c_str());
        }
    }
    else if (intent == "voice_query") {
        Serial.println("🎤 Voice query received");
        // Log the query for processing
        if (!logEntry.isEmpty()) {
            Serial.printf("QUERY LOG: %s\n", logEntry.c_str());
        }
        // Voice queries typically get responses
    }
    else if (intent == "memory_store") {
        Serial.println("💾 Memory storage request");
        // Log memory item
        if (!logEntry.isEmpty()) {
            Serial.printf("MEMORY STORED: %s\n", logEntry.c_str());
        }
        // Confirm storage if requested
    }
    else if (intent == "navigation_query") {
        Serial.println("🧭 Navigation query received");
        // Log navigation request
        if (!logEntry.isEmpty()) {
            Serial.printf("NAVIGATION LOG: %s\n", logEntry.c_str());
        }
    }
    else if (intent == "hand_gesture") {
        Serial.println("👋 Hand gesture detected");
        // Log gesture information
        if (!logEntry.isEmpty()) {
            Serial.printf("GESTURE LOG: %s\n", logEntry.c_str());
        }
    }
    else {
        // Handle unknown intents
        Serial.printf("❓ Unknown intent: %s\n", intent.c_str());
        if (!logEntry.isEmpty()) {
            Serial.printf("UNKNOWN INTENT LOG: %s\n", logEntry.c_str());
        }
    }
    
    // Handle routing if specified
    if (!routeTo.isEmpty()) {
        Serial.printf("Routing to: %s with params: %s\n", routeTo.c_str(), routeParams.c_str());
        // Add routing logic here as needed
    }
}

String cleanTextForWakeWord(const String& text) {
    String cleaned = text;
    cleaned.toLowerCase();
    
    // Remove common punctuation and extra spaces
    cleaned.replace(".", "");
    cleaned.replace(",", "");
    cleaned.replace("!", "");
    cleaned.replace("?", "");
    cleaned.replace(";", "");
    cleaned.replace(":", "");
    cleaned.replace("-", "");
    cleaned.replace("_", "");
    cleaned.replace("'", "");
    cleaned.replace("\"", "");
    
    // Replace multiple spaces with single space
    while (cleaned.indexOf("  ") != -1) {
        cleaned.replace("  ", " ");
    }
    
    // Trim leading/trailing spaces
    cleaned.trim();
    
    return cleaned;
}

void initializeLanguageSettings() {
    Serial.println("🌐 Initializing language settings...");
    
    // Check WiFi connection first
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("⚠️ WiFi not connected, using default language (en)");
        return;
    }
    
    // Fetch settings from API
    UserSettings settings = settingsManager.getSettings();
    if (settings.isValid) {
        String language = settings.language;
        Serial.printf("✅ Language setting retrieved: %s\n", language.c_str());
        
        // Set language for both STT and TTS
        deepgramClient.setDefaultLanguage(language);
        tts.setDefaultLanguage(language);
        
        Serial.printf("🎤 Deepgram STT language set to: %s\n", language.c_str());
        Serial.printf("🔊 Deepgram TTS language set to: %s\n", language.c_str());
    } else {
        Serial.println("⚠️ Failed to fetch language settings, using defaults");
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("Starting setup...");
    
    // Print core info
    Serial.printf("Setup running on core: %d\n", xPortGetCoreID());
    
    // Create synchronization primitives
    audioMutex = xSemaphoreCreateMutex();
    commandQueue = xQueueCreate(5, sizeof(String));
    audioCommandQueue = xQueueCreate(5, sizeof(AudioCommand)); // Create audio command queue
    
    if (!audioMutex || !commandQueue || !audioCommandQueue) {
        Serial.println("CRITICAL: Failed to create synchronization primitives!");
        while (true) delay(1000);
    }
    
    // Print memory info at startup
    Serial.printf("🔧 Startup memory info:\n");
    Serial.printf("   Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("   Heap size: %d bytes\n", ESP.getHeapSize());
    if (psramFound()) {
        Serial.printf("   PSRAM found: %d bytes free\n", ESP.getFreePsram());
    } else {
        Serial.println("   PSRAM: Not found");
    }
    
    // Check if PSRAM is available
    if (!psramFound()) {
        Serial.println("PSRAM not found! Using regular malloc instead.");
        audio_buffer = (uint8_t*)malloc(AUDIO_BUFFER_SIZE);
    } else {
        Serial.println("PSRAM found, using ps_malloc.");
        audio_buffer = (uint8_t*)ps_malloc(AUDIO_BUFFER_SIZE);
    }
    
    if (!audio_buffer) {
        Serial.println("CRITICAL: Failed to allocate audio buffer!");
        Serial.printf("Tried to allocate %d bytes\n", AUDIO_BUFFER_SIZE);
        Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
        if (psramFound()) {
            Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
        }
        while (true) {
            delay(1000);
        }
    }
    
    // Initialize buffer to zero
    memset(audio_buffer, 0, AUDIO_BUFFER_SIZE);
    Serial.printf("✅ Successfully allocated %d bytes for audio buffer\n", AUDIO_BUFFER_SIZE);
    Serial.printf("Audio buffer address: %p\n", audio_buffer);

    // Initialize the vision assistant (includes GPS initialization) on Core 1
    Serial.println("Initializing Vision Assistant on Core 1...");
    while (!visionAssistant.initialize()) {
        Serial.println("Failed to initialize Vision Assistant! Retrying in 2 seconds...");
        delay(2000);
    }
    Serial.println("Vision Assistant initialized successfully!");
    
    // Initialize language settings after WiFi is connected
    initializeLanguageSettings();
    
    // Start audio task on Core 0 (microphone will be initialized there)
    Serial.println("Starting audio task on Core 0...");
    xTaskCreatePinnedToCore(
        audioTask,           // Task function
        "AudioTask",         // Task name
        8192,               // Stack size (increased for audio processing)
        NULL,               // Parameters
        2,                  // Priority (higher than main loop)
        &AudioTaskHandle,   // Task handle
        0                   // Core 0
    );
    
    if (AudioTaskHandle == NULL) {
        Serial.println("CRITICAL: Failed to create audio task!");
        Serial.println("This could be due to insufficient memory or system resources.");
        Serial.println("Retrying audio task creation in 3 seconds...");
        delay(3000);
        
        // Try to create the task again in a loop until it succeeds
        while (AudioTaskHandle == NULL) {
            Serial.println("Retrying audio task creation...");
            xTaskCreatePinnedToCore(
                audioTask,           // Task function
                "AudioTask",         // Task name
                8192,               // Stack size (increased for audio processing)
                NULL,               // Parameters
                2,                  // Priority (higher than main loop)
                &AudioTaskHandle,   // Task handle
                0                   // Core 0
            );
            
            if (AudioTaskHandle == NULL) {
                Serial.printf("Audio task creation failed. Free heap: %d bytes. Retrying in 5 seconds...\n", ESP.getFreeHeap());
                delay(5000);
            } else {
                Serial.println("Audio task created successfully on retry!");
            }
        }
    }
    
    // Give the audio task time to initialize (increased like working demo)
    Serial.println("Waiting for audio task to initialize...");
    delay(5000);
    
    // Print GPS status
    Serial.println("GPS Status at startup:");
    GPSData gpsData = visionAssistant.getCurrentGPSData();
    if (gpsData.isValid) {
        Serial.println("GPS: " + visionAssistant.getGPSString());
    } else {
        Serial.println("GPS: Searching for satellites...");
    }
    
    // Try to initialize TTS with Deepgram API key
    if (tts.initialize(DEEPGRAM_API_KEY)) {
        Serial.println("TTS initialized successfully!");
        ttsAvailable = true;
    } else {
        Serial.println("Initial TTS initialization failed - will try lazy initialization");
        ttsAvailable = false;
    }
    
    // Set the tool callback
    visionAssistant.setToolCallback(toolHandler);
    
    Serial.printf("Vision Assistant setup complete on core %d - starting main loop\n", xPortGetCoreID());
}

void process_audio() {
    // Check if audio buffer is allocated
    if (!audio_buffer) {
        static unsigned long last_warning = 0;
        if (millis() - last_warning > 5000) {  // Warn every 5 seconds
            Serial.println("WARNING: Audio buffer not allocated, skipping audio processing");
            last_warning = millis();
        }
        return;
    }
    
    const int read_buffer_size = 512;
    int32_t raw_buffer[read_buffer_size];
    size_t bytes_read = 0;

    esp_err_t result = read_microphone_data(raw_buffer, sizeof(raw_buffer), &bytes_read);

    if (result == ESP_OK && bytes_read > 0) {
        int samples_read = bytes_read / sizeof(int32_t);
        
        // Debug: Log microphone reading stats occasionally
        static unsigned long last_read_debug = 0;
        static int total_reads = 0;
        static size_t total_bytes = 0;
        total_reads++;
        total_bytes += bytes_read;
        
        if (millis() - last_read_debug > 5000) {  // Every 5 seconds
            float avg_bytes_per_read = (float)total_bytes / total_reads;
            float effective_sample_rate = (total_bytes / sizeof(int32_t)) / ((millis() - last_read_debug) / 1000.0);
            Serial.printf("🎤 Mic stats: %d reads, avg %.1f bytes/read, ~%.0f samples/sec\n", 
                         total_reads, avg_bytes_per_read, effective_sample_rate);
            last_read_debug = millis();
            total_reads = 0;
            total_bytes = 0;
        }
        
        // Ensure we don't overflow the audio buffer
        for (int i = 0; i < samples_read; i++) {
            // Convert 32-bit sample to 16-bit (same as working demo)
            int32_t sample = raw_buffer[i] >> 14;

            // Apply digital gain (reduced from 5 to 2 like working demo)
            sample = sample * 2;

            // Clip to 16-bit range to prevent overflow
            if (sample > 32767) {
                sample = 32767;
            } else if (sample < -32768) {
                sample = -32768;
            }
            
            int16_t sample16 = (int16_t)sample;
            uint8_t* sample_bytes = (uint8_t*)&sample16;
            
            // Store as little-endian 16-bit samples
            if (audio_buffer_index + 1 < AUDIO_BUFFER_SIZE) {
                audio_buffer[audio_buffer_index++] = sample_bytes[0];
                audio_buffer[audio_buffer_index++] = sample_bytes[1];
            } else {
                // Buffer is full, wrap to beginning (circular buffer)
                buffer_has_wrapped = true;
                audio_buffer_index = 0;
                audio_buffer[audio_buffer_index++] = sample_bytes[0];
                audio_buffer[audio_buffer_index++] = sample_bytes[1];
            }
            
            // Increment sequence every 1000 samples to track buffer updates
            if ((audio_buffer_index % 2000) == 0) {
                buffer_sequence++;
            }
        }
    } else if (result != ESP_OK) {
        static unsigned long last_error = 0;
        if (millis() - last_error > 10000) {  // Log error every 10 seconds
            Serial.printf("Microphone read error: %s\n", esp_err_to_name(result));
            last_error = millis();
        }
    }
}

// Audio processing task running on Core 0
void audioTask(void *pvParameters) {
    Serial.println("Audio task started on Core 0");
    
    // Initialize the microphone on Core 0
    Serial.println("Initializing microphone on Core 0...");
    setup_microphone();
    Serial.println("Microphone initialized successfully on Core 0!");
    
    // Give microphone additional time to stabilize (like in working demo)
    delay(1000);
    
    unsigned long last_stt_time = 0;
    unsigned long recording_start_time = 0;
    
    while (true) {
        // Check for commands from the main core
        AudioCommand receivedCmd;
        if (xQueueReceive(audioCommandQueue, &receivedCmd, 0) == pdTRUE) {
            bool micWasActive = is_microphone_active();
            if (micWasActive) {
                stop_microphone();
            }

            if (receivedCmd.type == AudioCommandType::SPEAK_TEXT) {
                Serial.printf("🎤 Audio task received SPEAK_TEXT: \"%s\"\n", receivedCmd.text);
                tts.speakText(String(receivedCmd.text));
            } else if (receivedCmd.type == AudioCommandType::PLAY_DING) {
                Serial.println("🎤 Audio task received PLAY_DING");
                playDingSound();
            }

            if (micWasActive) {
                setup_microphone();
            }
        }

        // Process audio data
        if (audio_buffer && xSemaphoreTake(audioMutex, portMAX_DELAY)) {
            process_audio();
            xSemaphoreGive(audioMutex);
        }

        // Speech-to-text processing every 3 seconds for wake word detection
        // Note: Wake word detection uses Deepgram's search API for acoustic pattern matching
        // Command transcription after wake word still uses regular transcription API
        if (millis() - last_stt_time > 3000) {
            last_stt_time = millis();

            // Check if audio buffer is allocated and has sufficient data (use buffer wrap or minimum threshold)
            if (!audio_buffer || (!buffer_has_wrapped && audio_buffer_index < 8000)) {
                // Add debug info about buffer state
                static unsigned long last_debug = 0;
                if (millis() - last_debug > 10000) {  // Debug every 10 seconds
                    Serial.printf("Audio buffer state: allocated=%s, index=%d, required=8000\n", 
                                audio_buffer ? "yes" : "no", audio_buffer_index);
                    
                    // Show some sample values to see if we're getting real audio data
                    if (audio_buffer && audio_buffer_index > 100) {
                        int16_t* samples = (int16_t*)audio_buffer;
                        Serial.printf("Sample audio values: %d, %d, %d, %d, %d\n", 
                                    samples[0], samples[1], samples[10], samples[50], samples[100]);
                    }
                    
                    // Check if buffer is wrapping too frequently
                    Serial.printf("Buffer wrap status: wrapped=%s, sequence=%u\n", 
                                buffer_has_wrapped ? "yes" : "no", buffer_sequence);
                    last_debug = millis();
                }
                continue;
            }

            // Create a temporary buffer with the last 3 seconds of audio
            uint8_t* temp_buffer = (uint8_t*)malloc(AUDIO_BUFFER_SIZE);
            if (!temp_buffer) {
                Serial.println("Failed to allocate temp buffer for transcription");
                continue;
            }
            
            int current_index;
            bool has_wrapped;
            uint32_t current_sequence;
            static uint32_t last_transcribed_sequence = 0;
            
            if (xSemaphoreTake(audioMutex, portMAX_DELAY)) {
                current_index = audio_buffer_index;
                has_wrapped = buffer_has_wrapped;
                current_sequence = buffer_sequence;
                
                // Only transcribe if we have new audio data
                if (current_sequence == last_transcribed_sequence) {
                    xSemaphoreGive(audioMutex);
                    free(temp_buffer);
                    continue; // Skip transcription - no new audio
                }
                
                // Copy the most recent audio data in proper circular buffer order
                if (has_wrapped) {
                    // Buffer has wrapped - copy from current position to end, then from start to current
                    int bytes_from_current_to_end = AUDIO_BUFFER_SIZE - current_index;
                    memcpy(temp_buffer, audio_buffer + current_index, bytes_from_current_to_end);
                    memcpy(temp_buffer + bytes_from_current_to_end, audio_buffer, current_index);
                } else {
                    // Buffer hasn't wrapped yet, copy what we have and pad with zeros
                    memcpy(temp_buffer, audio_buffer, current_index);
                    memset(temp_buffer + current_index, 0, AUDIO_BUFFER_SIZE - current_index);
                }
                
                // Update the last transcribed sequence
                last_transcribed_sequence = current_sequence;
                
                // Debug: show buffer state
                static unsigned long lastBufferDebug = 0;
                if (millis() - lastBufferDebug > 5000) {
                    Serial.printf("Buffer debug: index=%d, wrapped=%s, seq=%u, samples=[%d,%d,%d]\n", 
                                current_index, has_wrapped ? "yes" : "no", current_sequence,
                                ((int16_t*)audio_buffer)[0], ((int16_t*)audio_buffer)[1], ((int16_t*)audio_buffer)[2]);
                    lastBufferDebug = millis();
                }
                
                xSemaphoreGive(audioMutex);
            }

            // Only transcribe if we have enough real audio data (not just zeros)
            bool hasRealAudio = false;
            int16_t* samples = (int16_t*)temp_buffer;
            int sampleCount = AUDIO_BUFFER_SIZE / 2;
            for (int i = 0; i < sampleCount; i++) {
                if (abs(samples[i]) > 200) { // Threshold for real audio
                    hasRealAudio = true;
                    break;
                }
            }

            bool wakeWordDetected = false;
            if (hasRealAudio) {
                // Use Deepgram's search API for wake word detection instead of transcription
                Serial.println("🔍 Searching for wake words using Deepgram search API...");
                wakeWordDetected = deepgramClient.searchForWakeWords(temp_buffer, AUDIO_BUFFER_SIZE, WAKE_WORDS, WAKE_WORDS_COUNT, 0.3f);
                
                if (wakeWordDetected) {
                    Serial.println("✅ Wake word detected via search API!");
                }
            } else {
                Serial.println("No real audio detected in buffer, skipping wake word search");
            }

            free(temp_buffer);

            if (wakeWordDetected) {
                Serial.println("🎙️ Wake word detected via Deepgram search API!");
                
                // First, force stop the microphone to ensure I2S is freed up for the ding sound
                stop_microphone();
                I2SManager::forceReleaseI2SAccess(); // Force release any lingering I2S access
                
                // Play confirmation ding sound (now that I2S is free)
                playDingSound();
                
                // Restart the microphone for recording the command
                setup_microphone();
                
                is_recording = true;
                recording_start_time = millis();
                Serial.println("Recording...");
                
                if (xSemaphoreTake(audioMutex, portMAX_DELAY)) {
                    audio_buffer_index = 0; // Clear buffer to start recording new audio
                    buffer_has_wrapped = false; // Reset wrap flag
                    buffer_sequence++; // Increment sequence for new recording
                    memset(audio_buffer, 0, AUDIO_BUFFER_SIZE); // Clear the buffer like in working demo
                    xSemaphoreGive(audioMutex);
                }
            }
        }

        // Handle recording
        if (is_recording) {
            if (recording_start_time == 0) {
                recording_start_time = millis();
            }

            if (millis() - recording_start_time > 5000) { // Record for 5 seconds
                is_recording = false;
                recording_start_time = 0;
                Serial.println("Recording finished. Processing command...");
                
                // Only process if we have sufficient audio data
                if (audio_buffer && audio_buffer_index >= 16000) {
                    uint8_t* command_buffer = (uint8_t*)malloc(audio_buffer_index);
                    if (command_buffer && xSemaphoreTake(audioMutex, portMAX_DELAY)) {
                        memcpy(command_buffer, audio_buffer, audio_buffer_index);
                        int buffer_size = audio_buffer_index;
                        xSemaphoreGive(audioMutex);
                        
                        String command = deepgramClient.transcribe(command_buffer, buffer_size);
                        Serial.println("Command: " + command);

                        if (!command.isEmpty()) {
                            // Send command to main core via queue
                            if (xQueueSend(commandQueue, &command, 0) != pdTRUE) {
                                Serial.println("Failed to queue command");
                            }
                        }
                        
                        free(command_buffer);
                    } else {
                        Serial.println("Failed to allocate command buffer or get mutex");
                    }
                } else {
                    Serial.println("Not enough audio data recorded, skipping transcription");
                }
            }
        }

        // Small delay to prevent watchdog issues
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void loop() {
    // Ensure we don't start processing until setup is complete
    static bool setup_done = false;
    if (!setup_done) {
        static unsigned long setup_start = millis();
        if (millis() - setup_start < 2000) {  // Wait 2 seconds after setup
            delay(10);
            return;
        }
        setup_done = true;
        Serial.printf("Setup delay complete, starting main loop processing on core %d\n", xPortGetCoreID());
    }
    
    // Run the vision assistant (handles WebSocket communication, GPS updates, and frame processing)
    visionAssistant.run();
    
    // Check for commands from the audio task
    String command;
    if (xQueueReceive(commandQueue, &command, 0) == pdTRUE) {
        Serial.printf("Received command from audio core: %s\n", command.c_str());
        visionAssistant.sendTextMessage(command);
    }

    // Print GPS status every 30 seconds
    static unsigned long lastGPSStatus = 0;
    if (millis() - lastGPSStatus > 30000) {
        GPSData gpsData = visionAssistant.getCurrentGPSData();
        if (gpsData.isValid) {
            Serial.println("GPS Status: " + visionAssistant.getGPSString());
        } else {
            Serial.println("GPS Status: No fix obtained");
        }
        lastGPSStatus = millis();
    }
    
    // Refresh language settings every 5 minutes
    static unsigned long lastLanguageUpdate = 0;
    if (millis() - lastLanguageUpdate > 300000) { // 5 minutes
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("🔄 Refreshing language settings...");
            String currentLanguage = settingsManager.getLanguage();
            deepgramClient.setDefaultLanguage(currentLanguage);
            tts.setDefaultLanguage(currentLanguage);
        }
        lastLanguageUpdate = millis();
    }
    
    // Small delay to prevent watchdog issues
    delay(10);
}
