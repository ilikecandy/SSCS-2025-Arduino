#include "vision_assistant.h"
#include "TTS.h"
#include "secrets.h"
#include <ArduinoJson.h>

VisionAssistant visionAssistant;
TTS tts;
bool ttsAvailable = false;

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
                if (!tts.speakText(message)) {
                    Serial.println("Failed to speak message via TTS.");
                }
            } else {
                Serial.println("TTS not available to speak message.");
            }
        }
        
        // Log the entry if provided
        if (!logEntry.isEmpty()) {
            Serial.printf("LOG: %s\n", logEntry.c_str());
        }
        
        // Handle routing if specified
        if (!routeTo.isEmpty()) {
            Serial.printf("Routing to: %s with params: %s\n", routeTo.c_str(), routeParams.c_str());
            // Add routing logic here as needed
        }
    }
}

void setup() {
    // Initialize the vision assistant (includes GPS initialization)
    if (!visionAssistant.initialize()) {
        Serial.println("Failed to initialize Vision Assistant!");
        while (true) {
            delay(1000);
        }
    }
    
    // // Give the camera system time to stabilize
    // Serial.println("Waiting for camera system to stabilize...");
    // delay(2000);
    
    // Print GPS status
    Serial.println("GPS Status at startup:");
    GPSData gpsData = visionAssistant.getCurrentGPSData();
    if (gpsData.isValid) {
        Serial.println("GPS: " + visionAssistant.getGPSString());
    } else {
        Serial.println("GPS: Searching for satellites...");
    }
    
    // Try to initialize TTS with Deepgram API key
    // If this fails, we'll attempt lazy initialization later
    if (tts.initialize(DEEPGRAM_API_KEY)) {
        Serial.println("TTS initialized successfully!");
        ttsAvailable = true;
    } else {
        Serial.println("Initial TTS initialization failed - will try lazy initialization");
        ttsAvailable = false;
        // Set the API key for potential lazy initialization
        // (The TTS class will store this for later use)
    }
    
    // Set the tool callback
    visionAssistant.setToolCallback(toolHandler);
    
    Serial.println("Vision Assistant setup complete - starting main loop");
}

void loop() {
    // Run the vision assistant (handles WebSocket communication, GPS updates, and frame processing)
    visionAssistant.run();
    
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
}
