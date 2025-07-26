#include "vision_assistant.h"
#include "TTS.h"
#include "secrets.h"

// Global instances
VisionAssistant visionAssistant;
TTS tts;
bool ttsAvailable = false;

// Custom response callback function with TTS integration
void customResponseHandler(const String& response) {
    Serial.println("=== Custom Response Handler ===");
    Serial.printf("Received response: %s\n", response.c_str());
    
    // Use TTS to speak the response - this will try lazy initialization if needed
    if (!tts.speakText(response)) {
        Serial.println("Failed to speak response via TTS - continuing without audio");
    }
    
    // Example: Parse response for specific keywords and take actions
    if (response.indexOf("person") != -1) {
        Serial.println("Person detected!");
        // Add your custom logic here
    }
    
    if (response.indexOf("car") != -1) {
        Serial.println("Car detected!");
        // Add your custom logic here
    }
    
    // You can add more sophisticated parsing here
    // Parse JSON responses, extract object coordinates, etc.
    Serial.println("=== End Custom Handler ===");
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
    
    // Set custom response callback (optional - comment out to use default)
    visionAssistant.setResponseCallback(customResponseHandler);
    
    Serial.println("Vision Assistant setup complete - starting main loop");
}

void loop() {
    // Run the vision assistant (handles WebSocket communication and frame processing)
    visionAssistant.run();
}
