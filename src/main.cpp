#include "vision_assistant.h"
#include "TTS.h"
#include "secrets.h"

// Global instances
VisionAssistant visionAssistant;
TTS tts;
bool ttsAvailable = false;

// Tool call handler for speaking messages
void toolHandler(const String& toolName, const String& message) {
    if (toolName == "speakMessage") {
        Serial.printf("Tool call received: %s - Message: %s\n", toolName.c_str(), message.c_str());
        if (ttsAvailable) {
            if (!tts.speakText(message)) {
                Serial.println("Failed to speak message via TTS.");
            }
        } else {
            Serial.println("TTS not available to speak message.");
        }
    }
}

void setup() {
    // Initialize the vision assistant
    if (!visionAssistant.initialize()) {
        Serial.println("Failed to initialize Vision Assistant!");
        while (true) {
            delay(1000);
        }
    }
    
    // Give the camera system time to stabilize
    Serial.println("Waiting for camera system to stabilize...");
    delay(2000);
    
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
    // Run the vision assistant (handles WebSocket communication and frame processing)
    visionAssistant.run();
}
