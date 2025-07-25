#include "vision_assistant.h"

// Global Vision Assistant instance
VisionAssistant visionAssistant;

// Custom response callback function
void customResponseHandler(const String& response) {
    Serial.println("=== Custom Response Handler ===");
    Serial.printf("Received response: %s\n", response.c_str());
    
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
    
    // Set custom response callback (optional - comment out to use default)
    visionAssistant.setResponseCallback(customResponseHandler);
    
    Serial.println("Vision Assistant setup complete - starting main loop");
}

void loop() {
    // Run the vision assistant (handles WebSocket communication and frame processing)
    visionAssistant.run();
}
