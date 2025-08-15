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

// Button Pin for Push-to-Talk and SOS
const int BUTTON_PIN = 15;

// Wake words (optimized for Deepgram's acoustic search - longer phrases work better)
const char* WAKE_WORDS[] = {
    "halo",
};
const int WAKE_WORDS_COUNT = sizeof(WAKE_WORDS) / sizeof(WAKE_WORDS[0]);

// Audio buffer for wake word detection (3 seconds)
const int WAKE_WORD_BUFFER_SECONDS = 3;
const int SAMPLE_RATE = 16000;
const int BITS_PER_SAMPLE = 16;
const int CHANNELS = 1;
const int WAKE_WORD_BUFFER_SIZE = WAKE_WORD_BUFFER_SECONDS * SAMPLE_RATE * (BITS_PER_SAMPLE / 8) * CHANNELS;

// Audio buffer for command recording (15 seconds maximum)
const int COMMAND_BUFFER_SECONDS = 15;
const int COMMAND_BUFFER_SIZE = COMMAND_BUFFER_SECONDS * SAMPLE_RATE * (BITS_PER_SAMPLE / 8) * CHANNELS;

uint8_t* wake_word_buffer = nullptr;
uint8_t* command_buffer = nullptr;
uint8_t* stt_temp_buffer = nullptr;
volatile int wake_word_buffer_index = 0;  // Made volatile for dual-core access
volatile int command_buffer_index = 0;    // For command recording
volatile bool is_recording = false;       // Made volatile for dual-core access
volatile bool wake_word_buffer_has_wrapped = false;  // Track if wake word buffer has wrapped around
volatile uint32_t buffer_sequence = 0;    // Sequence number to track buffer updates
volatile float baseline_audio_level = 0.0f; // Baseline audio level for silence detection
volatile bool baseline_calculated = false;  // Whether baseline has been calculated
volatile bool is_speaking = false; // Flag to prevent TTS overlap

// GPS and Places API
GPSData last_checked_gps_data;
unsigned long last_places_check_time = 0;

// Core synchronization
TaskHandle_t AudioTaskHandle = NULL;
SemaphoreHandle_t audioMutex;
QueueHandle_t commandQueue;
QueueHandle_t audioCommandQueue; // For sending commands to the audio task

// Enum for audio task commands
enum class AudioCommandType {
    SPEAK_TEXT,
    PLAY_DING,
    PLAY_BUTTON_DING,
    START_RECORDING,
    STOP_RECORDING_AND_PROCESS
};

// Struct for audio commands
struct AudioCommand {
    AudioCommandType type;
    char text[256]; // For SPEAK_TEXT command
};

// Struct for command messages (safer than String objects)
struct CommandMessage {
    char command[256]; // Fixed size buffer instead of String
};

// Function declarations
void audioTask(void *pvParameters);
void process_audio();
void playDingSound();
void playButtonDingSound();
String cleanTextForWakeWord(const String& text);
void sendEmergencyAlert(const String& alertType, const String& description);
void handleSystemAction(const JsonDocument& doc);
void initializeLanguageSettings();
void calculateBaselineAudioLevel();
bool isAudioSilent();
void processRecordedCommand();
void handleButton();
void checkAndAnnounceNearbyPlaces();
String stripHtmlTags(const String& html);

// Tool call handler for system actions
void toolHandler(const String& toolName, const String& jsonParams) {
    if (toolName == "systemAction") {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, jsonParams);
        if (error) {
            Serial.printf("Failed to parse systemAction JSON: %s\n", error.c_str());
            return;
        }
        
        // Conditionally print based on shouldSpeak to reduce log noise
        if (doc["shouldSpeak"].as<bool>()) {
            Serial.printf("systemAction call received with params: %s\n", jsonParams.c_str());
        }
        
        handleSystemAction(doc);
    } else if (toolName == "getDirections") {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, jsonParams);
        if (error) {
            Serial.printf("Failed to parse getDirections JSON: %s\n", error.c_str());
            return;
        }
        String destination = doc["destination"].as<String>();
        Serial.printf("getDirections call received with destination: %s\n", destination.c_str());

        GPSData origin = visionAssistant.getCurrentGPSData();
        if (!origin.isValid) {
            String message = "Sorry, I can't get directions without a valid GPS location.";
            AudioCommand cmd;
            cmd.type = AudioCommandType::SPEAK_TEXT;
            strncpy(cmd.text, message.c_str(), sizeof(cmd.text) - 1);
            cmd.text[sizeof(cmd.text) - 1] = '\0';
            xQueueSend(audioCommandQueue, &cmd, 0);
            return;
        }

        HTTPClient http;
        String url = "https://maps.googleapis.com/maps/api/directions/json?origin=" +
                     String(origin.latitude, 6) + "," + String(origin.longitude, 6) +
                     "&destination=" + destination +
                     "&key=" + String(GEMINI_API_KEY);
        
        http.begin(url);
        int httpResponseCode = http.GET();
        String directions = "";

        if (httpResponseCode == 200) {
            String payload = http.getString();
            JsonDocument dirDoc;
            deserializeJson(dirDoc, payload);

            if (dirDoc["status"] == "OK") {
                JsonArray steps = dirDoc["routes"][0]["legs"][0]["steps"];
                directions = "Starting route. ";
                for (int i = 0; i < steps.size() && i < 3; i++) { // Read out first 3 steps
                    String instruction = steps[i]["html_instructions"];
                    directions += stripHtmlTags(instruction) + ". ";
                }
            } else {
                directions = "Sorry, I could not find directions to " + destination;
            }
        } else {
            directions = "Sorry, there was an error getting directions.";
        }
        http.end();

        AudioCommand cmd;
        cmd.type = AudioCommandType::SPEAK_TEXT;
        strncpy(cmd.text, directions.c_str(), sizeof(cmd.text) - 1);
        cmd.text[sizeof(cmd.text) - 1] = '\0';
        if (xQueueSend(audioCommandQueue, &cmd, 0) != pdTRUE) {
            Serial.println("❌ Failed to queue SPEAK_TEXT command for directions");
        }
     } else {
         Serial.printf("Tool call handler invoked for unknown tool: %s\n", toolName.c_str());
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

void playButtonDingSound() {
    Serial.println("🔘 Playing command transcribed button ding...");
    
    // Generate a single-tone button click sound (shorter than wake word ding)
    const int dingDuration = 150;  // Shorter duration for button feedback
    const int sampleRate = 16000;
    const int totalSamples = (sampleRate * dingDuration) / 1000;
    
    // Allocate buffer for button ding sound
    uint8_t* dingBuffer = (uint8_t*)malloc(totalSamples * 2);  // 16-bit samples
    if (!dingBuffer) {
        Serial.println("❌ Failed to allocate button ding buffer");
        return;
    }
    
    int16_t* samples = (int16_t*)dingBuffer;
    
    // Generate single tone (1200 Hz - higher pitch than wake word ding)
    for (int i = 0; i < totalSamples; i++) {
        float t = (float)i / sampleRate;
        float amplitude = 0.25f * (1.0f - t * 6.67f);  // Quick fade out
        if (amplitude < 0) amplitude = 0;
        samples[i] = (int16_t)(amplitude * 6000 * sin(2 * PI * 1200 * t));
    }
    
    // Use TTS to play the button ding sound
    bool dingSuccess = false;
    if (ttsAvailable) {
        dingSuccess = tts.playAudioData(dingBuffer, totalSamples * 2);
        if (!dingSuccess) {
            Serial.println("❌ Failed to play button ding sound via TTS");
        }
    } else {
        Serial.println("❌ TTS not available for button ding sound");
    }
    
    // Clean up
    free(dingBuffer);
    
    if (dingSuccess) {
        Serial.println("🔘 Button ding complete - command transcribed!");
    } else {
        Serial.println("🔘 Button ding failed but continuing - command transcribed!");
    }
}

// Helper function to format ISO timestamp
String getISOTimestamp() {
    // Base timestamp: 2025-08-02T00:00:00Z (epoch for today)
    unsigned long currentMillis = millis();
    unsigned long totalSeconds = currentMillis / 1000;
    
    // Calculate hours, minutes, and seconds from device uptime
    unsigned long hours = (totalSeconds / 3600) % 24;
    unsigned long minutes = (totalSeconds / 60) % 60;
    unsigned long seconds = totalSeconds % 60;
    
    // Format as ISO timestamp (using current date as base)
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "2025-08-02T%02lu:%02lu:%02luZ", hours, minutes, seconds);
    return String(timestamp);
}

// Helper function to determine severity based on alert type
String getSeverityLevel(const String& alertType) {
    if (alertType == "fall_detection" || alertType == "medical_emergency" || alertType == "panic_button") {
        return "high";
    } else if (alertType == "obstacle_alert" || alertType == "lost_device") {
        return "medium";
    } else {
        return "low";
    }
}

void sendEmergencyAlert(const String& alertType, const String& description) {
    Serial.println("🚨 Emergency protocol activated!");
    Serial.printf("Alert Type: %s\n", alertType.c_str());
    Serial.printf("Description: %s\n", description.c_str());
    
    // Get current GPS location if available
    GPSData gpsData = visionAssistant.getCurrentGPSData();
    
    // Create location object
    JsonDocument locationObj;
    if (gpsData.isValid) {
        locationObj["latitude"] = gpsData.latitude;
        locationObj["longitude"] = gpsData.longitude;
        locationObj["address"] = "GPS Location"; // Could be enhanced with reverse geocoding
    } else {
        locationObj["latitude"] = nullptr;
        locationObj["longitude"] = nullptr;
        locationObj["address"] = "Location unavailable";
    }
    
    // Determine appropriate title and message based on alert type
    String title, message;
    if (alertType == "fall_detection") {
        title = "Emergency Alert - Fall Detected";
        message = description.isEmpty() ? "Fall detected by wearable device sensors" : description;
    } else if (alertType == "medical_emergency") {
        title = "Medical Emergency Alert";
        message = description.isEmpty() ? "Medical emergency detected" : description;
    } else if (alertType == "panic_button") {
        title = "Panic Alert";
        message = description.isEmpty() ? "Panic button activated by user" : description;
    } else if (alertType == "lost_device") {
        title = "Device Location Alert";
        message = description.isEmpty() ? "Device location tracking activated" : description;
    } else {
        title = "Safety Alert";
        message = description.isEmpty() ? "Emergency situation detected" : description;
    }
    
    // Create notification JSON in the specified format
    JsonDocument notificationDoc;
    notificationDoc["title"] = title;
    notificationDoc["message"] = message;
    notificationDoc["timestamp"] = getISOTimestamp();
    notificationDoc["severity"] = getSeverityLevel(alertType);
    notificationDoc["location"] = locationObj;
    notificationDoc["alert_type"] = alertType;
    notificationDoc["device_id"] = "esp32_camera";
    
    // Convert to string
    String jsonString;
    serializeJson(notificationDoc, jsonString);
    
    // Send POST request to notifications API
    HTTPClient http;
    http.begin(String(NOTIFICATIONS_API_URL) + "/notifications");
    http.addHeader("Content-Type", "application/json");
    
    Serial.println("📡 Sending emergency notification to API...");
    Serial.printf("URL: %s\n", String(NOTIFICATIONS_API_URL) + "/notifications");
    Serial.printf("JSON: %s\n", jsonString.c_str());
    
    int httpResponseCode = http.POST(jsonString);
    
    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.printf("✅ Emergency notification sent successfully! Response code: %d\n", httpResponseCode);
        Serial.printf("Response: %s\n", response.c_str());
    } else {
        Serial.printf("❌ Failed to send emergency notification. Error code: %d\n", httpResponseCode);
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
    // String routeTo = doc["routeTo"].as<String>();
    String routeParams = doc["routeParams"].as<String>();
    
    if (shouldSpeak) {
        Serial.printf("Intent: %s\n", intent.c_str());
        Serial.printf("Should Speak: %s\n", shouldSpeak ? "true" : "false");
        Serial.printf("Message: %s\n", message.c_str());
        Serial.printf("Log Entry: %s\n", logEntry.c_str());
        Serial.printf("Route Params: %s\n", routeParams.c_str());
    }
    
    // Handle speaking if required
    if (shouldSpeak && !message.isEmpty()) {
        if (is_speaking) {
            Serial.println("🗣️ TTS is already active, dropping new speak request.");
            return;
        }
        if (ttsAvailable) {
            is_speaking = true; // Set flag before sending
            AudioCommand cmd;
            cmd.type = AudioCommandType::SPEAK_TEXT;
            strncpy(cmd.text, message.c_str(), sizeof(cmd.text) - 1);
            cmd.text[sizeof(cmd.text) - 1] = '\0'; // Ensure null termination
            
            if (xQueueSend(audioCommandQueue, &cmd, 0) != pdTRUE) {
                Serial.println("❌ Failed to queue SPEAK_TEXT command");
                is_speaking = false; // Reset flag if queueing failed
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
        
        // Determine alert type based on message content and context
        String alertType = "fall_detection"; // Default type
        String description = message.isEmpty() ? "Emergency detected by vision assistant" : message;
        
        // Analyze message content to determine specific emergency type
        String lowerMessage = message;
        lowerMessage.toLowerCase();
        
        if (lowerMessage.indexOf("fall") != -1 || lowerMessage.indexOf("fell") != -1 || 
            lowerMessage.indexOf("down") != -1 || lowerMessage.indexOf("trip") != -1) {
            alertType = "fall_detection";
            if (description == message) {
                description = "Fall detected - " + message;
            }
        }
        else if (lowerMessage.indexOf("medical") != -1 || lowerMessage.indexOf("hurt") != -1 || 
                 lowerMessage.indexOf("pain") != -1 || lowerMessage.indexOf("sick") != -1 ||
                 lowerMessage.indexOf("emergency") != -1) {
            alertType = "medical_emergency";
            if (description == message) {
                description = "Medical emergency - " + message;
            }
        }
        else if (lowerMessage.indexOf("help") != -1 || lowerMessage.indexOf("panic") != -1 || 
                 lowerMessage.indexOf("scared") != -1 || lowerMessage.indexOf("danger") != -1) {
            alertType = "panic_button";
            if (description == message) {
                description = "Panic alert - " + message;
            }
        }
        else if (lowerMessage.indexOf("lost") != -1 || lowerMessage.indexOf("find") != -1 || 
                 lowerMessage.indexOf("location") != -1) {
            alertType = "lost_device";
            if (description == message) {
                description = "Location assistance - " + message;
            }
        }
        else if (lowerMessage.indexOf("unresponsive") != -1 || lowerMessage.indexOf("no response") != -1 ||
                 lowerMessage.indexOf("unconscious") != -1) {
            alertType = "medical_emergency";
            if (description == message) {
                description = "User unresponsive - " + message;
            }
        }
        
        // Log the emergency details
        if (!logEntry.isEmpty()) {
            Serial.printf("EMERGENCY LOG: %s\n", logEntry.c_str());
        }
        
        // Send emergency alert with determined type
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
    // if (!routeTo.isEmpty()) {
    //     Serial.printf("Routing to: %s with params: %s\n", routeTo.c_str(), routeParams.c_str());
    //     // Add routing logic here as needed
    // }
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

void calculateBaselineAudioLevel() {
    if (!command_buffer || command_buffer_index < 1600) { // Need at least 0.1 seconds of audio
        return;
    }
    
    // Calculate RMS of the first 0.5 seconds of recording for baseline
    int total_samples = command_buffer_index / 2; // Total number of 16-bit samples available
    int samples_to_analyze = min(total_samples, 8000); // 0.5 seconds max, but not more than available
    
    if (samples_to_analyze <= 0) {
        return; // Safety check
    }
    
    int16_t* samples = (int16_t*)command_buffer;
    
    float sum_squares = 0.0f;
    for (int i = 0; i < samples_to_analyze; i++) {
        float sample = (float)samples[i];
        sum_squares += sample * sample;
    }
    
    baseline_audio_level = sqrt(sum_squares / samples_to_analyze);
    baseline_calculated = true;
    
    Serial.printf("📊 Baseline audio level calculated: %.2f (from %d samples)\n", baseline_audio_level, samples_to_analyze);
}

bool isAudioSilent() {
    if (!baseline_calculated || !command_buffer || command_buffer_index < 16000) { // Need at least 1 second of audio (16000 bytes)
        return false;
    }
    
    // Check the last 1 second of audio for silence - ensure we don't go out of bounds
    int total_samples = command_buffer_index / 2; // Total number of 16-bit samples
    int samples_to_check = min(16000, total_samples); // 1 second max, but not more than available
    int start_index = max(0, total_samples - samples_to_check); // Start from a safe position
    
    if (start_index >= total_samples || samples_to_check <= 0) {
        return false; // Safety check
    }
    
    int16_t* samples = (int16_t*)command_buffer;
    
    float sum_squares = 0.0f;
    for (int i = start_index; i < start_index + samples_to_check && i < total_samples; i++) {
        float sample = (float)samples[i];
        sum_squares += sample * sample;
    }
    
    float current_level = sqrt(sum_squares / samples_to_check);
    
    // Consider it silent if current level is less than 1.2x baseline + small threshold (made more sensitive)
    float silence_threshold = baseline_audio_level * 1.2f + 50.0f;
    
    Serial.printf("🔇 Silence check: current=%.2f, baseline=%.2f, threshold=%.2f, silent=%s\n", 
                 current_level, baseline_audio_level, silence_threshold, 
                 (current_level < silence_threshold) ? "YES" : "NO");
    
    return current_level < silence_threshold;
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
    commandQueue = xQueueCreate(5, sizeof(CommandMessage)); // Use CommandMessage struct instead of String
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
    
    // Check if PSRAM is available and allocate buffers
    if (!psramFound()) {
        Serial.println("PSRAM not found! Using regular malloc instead.");
        wake_word_buffer = (uint8_t*)malloc(WAKE_WORD_BUFFER_SIZE);
        command_buffer = (uint8_t*)malloc(COMMAND_BUFFER_SIZE);
    } else {
        Serial.println("PSRAM found, using ps_malloc.");
        wake_word_buffer = (uint8_t*)ps_malloc(WAKE_WORD_BUFFER_SIZE);
        command_buffer = (uint8_t*)ps_malloc(COMMAND_BUFFER_SIZE);
        stt_temp_buffer = (uint8_t*)ps_malloc(WAKE_WORD_BUFFER_SIZE);
    }
    
    if (!wake_word_buffer || !command_buffer || !stt_temp_buffer) {
        Serial.println("CRITICAL: Failed to allocate audio buffers!");
        Serial.printf("Tried to allocate wake word buffer: %d bytes\n", WAKE_WORD_BUFFER_SIZE);
        Serial.printf("Tried to allocate command buffer: %d bytes\n", COMMAND_BUFFER_SIZE);
        Serial.printf("Tried to allocate stt temp buffer: %d bytes\n", WAKE_WORD_BUFFER_SIZE);
        Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
        if (psramFound()) {
            Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
        }
        while (true) {
            delay(1000);
        }
    }
    
    // Initialize buffers to zero
    memset(wake_word_buffer, 0, WAKE_WORD_BUFFER_SIZE);
    memset(command_buffer, 0, COMMAND_BUFFER_SIZE);
    Serial.printf("✅ Successfully allocated wake word buffer: %d bytes\n", WAKE_WORD_BUFFER_SIZE);
    Serial.printf("✅ Successfully allocated command buffer: %d bytes\n", COMMAND_BUFFER_SIZE);
    Serial.printf("Wake word buffer address: %p\n", wake_word_buffer);
    Serial.printf("Command buffer address: %p\n", command_buffer);

    // Initialize Deepgram client now that we know PSRAM is available
    if (!deepgramClient.begin()) {
        Serial.println("CRITICAL: Failed to initialize DeepgramClient!");
        while(true) delay(1000);
    }

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
    
    // Set up button pin
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    Serial.println("Button pin initialized.");
    
    // Play a ding sound to indicate setup is complete
    Serial.println("✅ Setup complete! Playing notification sound...");
    AudioCommand setupCompleteDingCmd;
    setupCompleteDingCmd.type = AudioCommandType::PLAY_BUTTON_DING;
    if (xQueueSend(audioCommandQueue, &setupCompleteDingCmd, 0) != pdTRUE) {
        Serial.println("❌ Failed to queue setup complete ding command");
    }
    
    Serial.printf("Vision Assistant setup complete on core %d - starting main loop\n", xPortGetCoreID());
}

void process_audio() {
    // Check if audio buffers are allocated
    if (!wake_word_buffer || !command_buffer) {
        static unsigned long last_warning = 0;
        if (millis() - last_warning > 5000) {  // Warn every 5 seconds
            Serial.println("WARNING: Audio buffers not allocated, skipping audio processing");
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
        
        // Process each sample
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
            
            // Store in wake word buffer (continuous circular buffer) - ensure we have space for 2 bytes
            if (wake_word_buffer_index + 2 <= WAKE_WORD_BUFFER_SIZE) {
                wake_word_buffer[wake_word_buffer_index] = sample_bytes[0];
                wake_word_buffer[wake_word_buffer_index + 1] = sample_bytes[1];
                wake_word_buffer_index += 2;
            } else {
                // Buffer is full, wrap to beginning (circular buffer)
                wake_word_buffer_has_wrapped = true;
                wake_word_buffer_index = 0;
                if (wake_word_buffer_index + 2 <= WAKE_WORD_BUFFER_SIZE) {
                    wake_word_buffer[wake_word_buffer_index] = sample_bytes[0];
                    wake_word_buffer[wake_word_buffer_index + 1] = sample_bytes[1];
                    wake_word_buffer_index += 2;
                }
            }
            
            // Store in command buffer if recording - ensure we have space for 2 bytes
            if (is_recording && command_buffer_index + 2 <= COMMAND_BUFFER_SIZE) {
                command_buffer[command_buffer_index] = sample_bytes[0];
                command_buffer[command_buffer_index + 1] = sample_bytes[1];
                command_buffer_index += 2;
            }
            
            // Increment sequence every 1000 samples to track buffer updates
            if ((wake_word_buffer_index % 2000) == 0) {
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
                is_speaking = false; // Reset flag after speaking is done
            } else if (receivedCmd.type == AudioCommandType::PLAY_DING) {
                Serial.println("🎤 Audio task received PLAY_DING");
                playDingSound();
            } else if (receivedCmd.type == AudioCommandType::PLAY_BUTTON_DING) {
                Serial.println("🎤 Audio task received PLAY_BUTTON_DING");
                playButtonDingSound();
            } else if (receivedCmd.type == AudioCommandType::START_RECORDING) {
                Serial.println("🎤 Audio task received START_RECORDING");
                if (is_speaking) {
                    Serial.println("🚫 Button pressed during speech - cancelling TTS...");
                    tts.cancel();
                }
                
                is_recording = true;
                recording_start_time = millis();
                Serial.println("Recording command (button press)...");
                
                if (xSemaphoreTake(audioMutex, portMAX_DELAY)) {
                    command_buffer_index = 0;
                    baseline_calculated = false;
                    if (command_buffer) {
                        memset(command_buffer, 0, COMMAND_BUFFER_SIZE);
                    }
                    xSemaphoreGive(audioMutex);
                }
            } else if (receivedCmd.type == AudioCommandType::STOP_RECORDING_AND_PROCESS) {
                Serial.println("🎤 Audio task received STOP_RECORDING_AND_PROCESS");
                if (is_recording) {
                    processRecordedCommand();
                }
            }

            if (micWasActive) {
                setup_microphone();
            }
        }

        // Process audio data
        if ((wake_word_buffer || command_buffer) && xSemaphoreTake(audioMutex, portMAX_DELAY)) {
            process_audio();
            xSemaphoreGive(audioMutex);
        }

        // Speech-to-text processing every 1 second for wake word detection (only when not recording)
        // Note: Wake word detection uses Deepgram's search API for acoustic pattern matching
        // Command transcription after wake word still uses regular transcription API
        if (!is_recording && millis() - last_stt_time > 1000) {
            last_stt_time = millis();

            // Check if wake word buffer is allocated and has sufficient data
            if (!wake_word_buffer || (!wake_word_buffer_has_wrapped && wake_word_buffer_index < 8000)) {
                // Add debug info about buffer state
                static unsigned long last_debug = 0;
                if (millis() - last_debug > 10000) {  // Debug every 10 seconds
                    Serial.printf("Wake word buffer state: allocated=%s, index=%d, required=8000, wrapped=%s\n", 
                                wake_word_buffer ? "yes" : "no", wake_word_buffer_index, 
                                wake_word_buffer_has_wrapped ? "yes" : "no");
                    
                    // Show some sample values to see if we're getting real audio data
                    if (wake_word_buffer && wake_word_buffer_index > 100) {
                        int16_t* samples = (int16_t*)wake_word_buffer;
                        Serial.printf("Sample audio values: %d, %d, %d, %d, %d\n", 
                                    samples[0], samples[1], samples[10], samples[50], samples[100]);
                    }
                    
                    // Check buffer bounds
                    if (wake_word_buffer_index > WAKE_WORD_BUFFER_SIZE) {
                        Serial.printf("❌ CRITICAL: Wake word buffer index out of bounds: %d > %d\n", 
                                    wake_word_buffer_index, WAKE_WORD_BUFFER_SIZE);
                        wake_word_buffer_index = 0; // Reset to prevent corruption
                        wake_word_buffer_has_wrapped = false;
                    }
                    
                    Serial.printf("Buffer sequence: %u\n", buffer_sequence);
                    last_debug = millis();
                }
                continue;
            }

            // Create a temporary buffer with the last 3 seconds of audio
            if (!stt_temp_buffer) {
                Serial.println("STT temp buffer not allocated");
                continue;
            }
            uint8_t* temp_buffer = stt_temp_buffer;
            
            int current_index;
            bool has_wrapped;
            uint32_t current_sequence;
            static uint32_t last_transcribed_sequence = 0;
            
            if (xSemaphoreTake(audioMutex, portMAX_DELAY)) {
                current_index = wake_word_buffer_index;
                has_wrapped = wake_word_buffer_has_wrapped;
                current_sequence = buffer_sequence;
                
                // Only transcribe if we have new audio data
                if (current_sequence == last_transcribed_sequence) {
                    xSemaphoreGive(audioMutex);
                    continue; // Skip transcription - no new audio
                }
                
                // Copy the most recent audio data in proper circular buffer order
                if (has_wrapped) {
                    // Buffer has wrapped - copy from current position to end, then from start to current
                    int bytes_from_current_to_end = WAKE_WORD_BUFFER_SIZE - current_index;
                    memcpy(stt_temp_buffer, wake_word_buffer + current_index, bytes_from_current_to_end);
                    memcpy(stt_temp_buffer + bytes_from_current_to_end, wake_word_buffer, current_index);
                } else {
                    // Buffer hasn't wrapped yet, copy what we have and pad with zeros
                    memcpy(stt_temp_buffer, wake_word_buffer, current_index);
                    memset(stt_temp_buffer + current_index, 0, WAKE_WORD_BUFFER_SIZE - current_index);
                }
                
                // Update the last transcribed sequence
                last_transcribed_sequence = current_sequence;
                
                // Debug: show buffer state
                static unsigned long lastBufferDebug = 0;
                if (millis() - lastBufferDebug > 5000) {
                    Serial.printf("Buffer debug: index=%d, wrapped=%s, seq=%u, samples=[%d,%d,%d]\n", 
                                current_index, has_wrapped ? "yes" : "no", current_sequence,
                                ((int16_t*)wake_word_buffer)[0], ((int16_t*)wake_word_buffer)[1], ((int16_t*)wake_word_buffer)[2]);
                    lastBufferDebug = millis();
                }
                
                xSemaphoreGive(audioMutex);
            }

            // Only transcribe if we have enough real audio data (not just zeros)
            bool hasRealAudio = false;
            int16_t* samples = (int16_t*)stt_temp_buffer;
            int sampleCount = WAKE_WORD_BUFFER_SIZE / 2;
            for (int i = 0; i < sampleCount; i++) {
                if (abs(samples[i]) > 50) { // Threshold for real audio
                    hasRealAudio = true;
                    break;
                }
            }

            bool wakeWordDetected = false;
            if (hasRealAudio) {
                // Use Deepgram's search API for wake word detection instead of transcription
                Serial.println("🔍 Searching for wake words using Deepgram search API...");
                // TODO INCREASE CONFIDENCE
                wakeWordDetected = deepgramClient.searchForWakeWords(stt_temp_buffer, WAKE_WORD_BUFFER_SIZE, WAKE_WORDS, WAKE_WORDS_COUNT, 0.60f);
                
                if (wakeWordDetected) {
                    Serial.println("✅ Wake word detected via search API!");
                }
            } else {
                Serial.println("No real audio detected in buffer, skipping wake word search");
            }

            if (wakeWordDetected) {
                Serial.println("🎙️ Wake word detected via Deepgram search API!");
                
                // If TTS is active, cancel it immediately
                if (is_speaking) {
                    Serial.println("🚫 Wake word detected during speech - cancelling TTS...");
                    tts.cancel();
                }
                
                // Queue a ding sound to be played by the audio task, which will handle I2S switching
                AudioCommand dingCmd;
                dingCmd.type = AudioCommandType::PLAY_DING;
                if (xQueueSend(audioCommandQueue, &dingCmd, 0) != pdTRUE) {
                    Serial.println("❌ Failed to queue PLAY_DING command");
                }
                
                is_recording = true;
                recording_start_time = millis();
                Serial.println("Recording command (max 15 seconds)...");
                
                if (xSemaphoreTake(audioMutex, portMAX_DELAY)) {
                    command_buffer_index = 0; // Clear command buffer to start recording new audio
                    baseline_calculated = false; // Reset baseline
                    if (command_buffer) {
                        memset(command_buffer, 0, COMMAND_BUFFER_SIZE); // Clear the command buffer
                    }
                    xSemaphoreGive(audioMutex);
                }
            }
        }

        // Handle recording
        if (is_recording) {
            if (recording_start_time == 0) {
                recording_start_time = millis();
            }

            // Calculate baseline after first 0.5 seconds of recording
            if (!baseline_calculated && millis() - recording_start_time > 500) {
                if (xSemaphoreTake(audioMutex, portMAX_DELAY)) {
                    calculateBaselineAudioLevel();
                    xSemaphoreGive(audioMutex);
                }
            }

            // Check for silence after baseline is calculated and at least 3 seconds have passed
            bool shouldStopForSilence = false;
            if (baseline_calculated && millis() - recording_start_time > 3000) { // Wait 3 seconds before checking silence
                static unsigned long lastSilenceCheck = 0;
                static int consecutiveSilentChecks = 0; // Track consecutive silent readings
                const int requiredSilentChecks = 3; // Need 3 consecutive checks (0.6 seconds total)
                
                if (millis() - lastSilenceCheck > 200) { // Check every 200ms
                    if (xSemaphoreTake(audioMutex, portMAX_DELAY)) {
                        // Safety check: ensure command buffer index is within bounds
                        if (command_buffer_index > COMMAND_BUFFER_SIZE) {
                            Serial.printf("❌ CRITICAL: Command buffer overflow detected: %d > %d\n", 
                                        command_buffer_index, COMMAND_BUFFER_SIZE);
                            command_buffer_index = COMMAND_BUFFER_SIZE; // Cap it to prevent corruption
                        }
                        
                        bool currentlySilent = isAudioSilent();
                        if (currentlySilent) {
                            consecutiveSilentChecks++;
                            Serial.printf("🔇 Silent check %d/%d\n", consecutiveSilentChecks, requiredSilentChecks);
                        } else {
                            consecutiveSilentChecks = 0; // Reset counter if not silent
                        }
                        
                        // Only stop if we've had enough consecutive silent checks
                        shouldStopForSilence = (consecutiveSilentChecks >= requiredSilentChecks);
                        
                        xSemaphoreGive(audioMutex);
                    }
                    lastSilenceCheck = millis();
                    if (shouldStopForSilence) {
                        Serial.println("🔇 Sustained silence detected - stopping recording");
                    }
                }
            }

            // Stop recording if max time reached or silence detected
            if (millis() - recording_start_time > 15000 || shouldStopForSilence) { // 15 seconds max or silence
                if (shouldStopForSilence) {
                    Serial.println("Recording finished due to silence. Processing command...");
                } else {
                    Serial.println("Recording finished (15s max). Processing command...");
                }
                processRecordedCommand();
            }
        }

        // Small delay to prevent watchdog issues
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void processRecordedCommand() {
    is_recording = false;
    
    // Play a ding sound to indicate the command was transcribed
    AudioCommand buttonDingCmd;
    buttonDingCmd.type = AudioCommandType::PLAY_BUTTON_DING;
    if (xQueueSend(audioCommandQueue, &buttonDingCmd, 0) != pdTRUE) {
        Serial.println("❌ Failed to queue PLAY_BUTTON_DING command");
    }

    if (command_buffer && command_buffer_index > 8000) { // Need at least 0.5s of audio
        uint8_t* temp_command_buffer = (uint8_t*)malloc(command_buffer_index);
        if (temp_command_buffer && xSemaphoreTake(audioMutex, portMAX_DELAY)) {
            if (command_buffer_index <= COMMAND_BUFFER_SIZE) {
                memcpy(temp_command_buffer, command_buffer, command_buffer_index);
                int buffer_size = command_buffer_index;
                xSemaphoreGive(audioMutex);

                Serial.printf("🎤 Processing %d bytes of command audio\n", buffer_size);
                String command = deepgramClient.transcribe(temp_command_buffer, buffer_size);
                Serial.println("Command: " + command);

                if (!command.isEmpty()) {
                    CommandMessage cmdMsg;
                    strncpy(cmdMsg.command, command.c_str(), sizeof(cmdMsg.command) - 1);
                    cmdMsg.command[sizeof(cmdMsg.command) - 1] = '\0';
                    if (xQueueSend(commandQueue, &cmdMsg, 0) != pdTRUE) {
                        Serial.println("Failed to queue command");
                    }
                }
                free(temp_command_buffer);
            } else {
                Serial.printf("❌ Command buffer index out of bounds: %d > %d\n", command_buffer_index, COMMAND_BUFFER_SIZE);
                xSemaphoreGive(audioMutex);
                free(temp_command_buffer);
            }
        } else {
            Serial.println("Failed to allocate temp command buffer or get mutex");
            if (temp_command_buffer) free(temp_command_buffer);
        }
    } else {
        Serial.printf("Not enough audio data recorded: %d bytes\n", command_buffer_index);
    }
}

void handleButton() {
    static bool last_button_state = HIGH;
    static bool button_held_down = false;
    static int short_press_count = 0;
    static unsigned long last_press_time = 0;
    static unsigned long press_start_time = 0;

    bool current_button_state = digitalRead(BUTTON_PIN);

    // Button pressed
    if (current_button_state == LOW && last_button_state == HIGH) {
        delay(50); // Debounce
        if (digitalRead(BUTTON_PIN) == LOW) {
            press_start_time = millis();
            button_held_down = true;

            // Check if this press is part of an SOS sequence
            if (millis() - last_press_time < 1000) { // 1 second between presses for SOS
                short_press_count++;
            } else {
                short_press_count = 1; // Reset if too much time has passed
            }
            last_press_time = millis();

            Serial.printf("Button pressed, count: %d\n", short_press_count);
        }
    }
    // Button released
    else if (current_button_state == HIGH && last_button_state == LOW) {
        if (button_held_down) {
            unsigned long press_duration = millis() - press_start_time;

            if (press_duration < 500) { // Short press
                Serial.printf("Short press detected (duration: %lu ms)\n", press_duration);
                if (short_press_count >= 5) {
                    Serial.println("🆘 SOS condition met! Sending alert.");
                    sendEmergencyAlert("panic_button", "SOS button activated with 5 short presses.");
                    short_press_count = 0; // Reset after sending
                }
            } else { // Long press (push-to-talk)
                Serial.printf("Long press detected (duration: %lu ms) - stopping recording.\n", press_duration);
                short_press_count = 0; // Reset SOS count on long press
                AudioCommand cmd;
                cmd.type = AudioCommandType::STOP_RECORDING_AND_PROCESS;
                if (xQueueSend(audioCommandQueue, &cmd, 0) != pdTRUE) {
                    Serial.println("❌ Failed to queue STOP_RECORDING_AND_PROCESS command");
                }
            }
            button_held_down = false;
        }
    }

    // Start recording on initial press if it's not an SOS sequence that's about to trigger
    if (button_held_down && (millis() - press_start_time > 50) && (millis() - press_start_time < 100) && short_press_count < 5) {
        if (!is_recording) {
            Serial.println("Starting push-to-talk recording.");
            AudioCommand cmd;
            cmd.type = AudioCommandType::START_RECORDING;
            if (xQueueSend(audioCommandQueue, &cmd, 0) != pdTRUE) {
                Serial.println("❌ Failed to queue START_RECORDING command");
            }
        }
    }

    // Reset SOS count if time between presses is too long
    if (short_press_count > 0 && millis() - last_press_time > 1000) {
        // Serial.println("Resetting SOS count due to timeout.");
        short_press_count = 0;
    }

    last_button_state = current_button_state;
}

void checkAndAnnounceNearbyPlaces() {
    if (millis() - last_places_check_time < 30000) { // Check every 30 seconds
        return;
    }
    last_places_check_time = millis();

    GPSData current_gps_data = visionAssistant.getCurrentGPSData();
    if (!current_gps_data.isValid) {
        return;
    }

    // Check if user has moved more than 20 meters
    float distance = visionAssistant.calculateDistance(last_checked_gps_data.latitude, last_checked_gps_data.longitude, current_gps_data.latitude, current_gps_data.longitude);
    if (distance < 20.0 && last_checked_gps_data.isValid) {
        return;
    }

    last_checked_gps_data = current_gps_data;

    HTTPClient http;
    String url = "https://maps.googleapis.com/maps/api/place/nearbysearch/json?location=" +
                 String(current_gps_data.latitude, 6) + "," +
                 String(current_gps_data.longitude, 6) +
                 "&radius=50&key=" + String(GEMINI_API_KEY);

    http.begin(url);
    int httpResponseCode = http.GET();

    if (httpResponseCode == 200) {
        String payload = http.getString();
        JsonDocument doc;
        deserializeJson(doc, payload);

        if (doc["results"].size() > 0) {
            String place_name = doc["results"][0]["name"].as<String>();
            String message = "You are entering " + place_name;
            Serial.println(message);

            AudioCommand cmd;
            cmd.type = AudioCommandType::SPEAK_TEXT;
            strncpy(cmd.text, message.c_str(), sizeof(cmd.text) - 1);
            cmd.text[sizeof(cmd.text) - 1] = '\0';
            if (xQueueSend(audioCommandQueue, &cmd, 0) != pdTRUE) {
                Serial.println("❌ Failed to queue SPEAK_TEXT command for nearby place");
            }
        }
    }
    http.end();
}

String stripHtmlTags(const String& html) {
    String text = "";
    bool inTag = false;
    for (char c : html) {
        if (c == '<') {
            inTag = true;
        } else if (c == '>') {
            inTag = false;
        } else if (!inTag) {
            text += c;
        }
    }
    return text;
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
    
    // Handle button presses for push-to-talk and SOS
    handleButton();

    // Run the vision assistant (handles WebSocket communication, GPS updates, and frame processing)
    visionAssistant.run();

    // Check for nearby places
    checkAndAnnounceNearbyPlaces();
    
    // Check for commands from the audio task
    CommandMessage cmdMsg;
    if (xQueueReceive(commandQueue, &cmdMsg, 0) == pdTRUE) {
        Serial.printf("Received command from audio core: %s\n", cmdMsg.command);
        
        // Play button ding sound to indicate command was transcribed and is being processed
        visionAssistant.sendTextMessage(String(cmdMsg.command)); // Convert to String only when needed locally
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
    
    // Set a 2-second delay in the main loop to control the Gemini sending rate.
    // Small delay to prevent watchdog issues
    delay(10);
}
