# Vision Assistant Class Usage

This document explains how to use the new `VisionAssistant` class that provides a clean callback-based API for handling Gemini vision responses.

## Overview

The `VisionAssistant` class encapsulates all the WebSocket communication, camera handling, and frame processing logic into a single, easy-to-use class with callback support.

## Key Features

- **Callback-based response handling**: Set custom functions to process Gemini responses
- **Automatic frame processing**: Handles camera capture, encoding, and transmission
- **Built-in rate limiting**: Processes frames at configurable intervals (default 500ms)
- **Error handling**: Robust error handling for camera and WebSocket issues
- **Static instance pattern**: Allows for easy callback integration

## Basic Usage

### 1. Simple Setup

```cpp
#include "vision_assistant.h"

VisionAssistant visionAssistant;

void setup() {
    // Initialize the vision assistant (handles WiFi, camera, WebSocket setup)
    if (!visionAssistant.initialize()) {
        Serial.println("Failed to initialize Vision Assistant!");
        while (true) delay(1000);
    }
}

void loop() {
    // Run the vision assistant (handles all communication and frame processing)
    visionAssistant.run();
}
```

### 2. Custom Response Handler

```cpp
// Define your custom response handler
void myResponseHandler(const String& response) {
    Serial.println("Custom handler received: " + response);
    
    // Add your custom logic here
    if (response.indexOf("person") != -1) {
        Serial.println("Person detected!");
        // Trigger some action
    }
}

void setup() {
    visionAssistant.initialize();
    
    // Set your custom response callback
    visionAssistant.setResponseCallback(myResponseHandler);
}
```

## Advanced Usage Examples

### Object Detection Handler

```cpp
void objectDetectionHandler(const String& response) {
    String lowerResponse = response;
    lowerResponse.toLowerCase();
    
    if (lowerResponse.indexOf("person") != -1) {
        Serial.println("🚶 Person detected!");
        // Trigger person-specific actions
    }
    
    if (lowerResponse.indexOf("car") != -1) {
        Serial.println("🚗 Vehicle detected!");
        // Trigger vehicle-specific actions
    }
}
```

### Security Monitoring Handler

```cpp
void securityHandler(const String& response) {
    String lowerResponse = response;
    lowerResponse.toLowerCase();
    
    if (lowerResponse.indexOf("person") != -1) {
        Serial.println("🚨 SECURITY ALERT: Person detected!");
        // Send notification, trigger alarm, start recording, etc.
    }
}
```

### Analytics Handler

```cpp
void analyticsHandler(const String& response) {
    static int responseCount = 0;
    responseCount++;
    
    Serial.printf("Response #%d at %lu ms: %s\n", 
                  responseCount, millis(), response.c_str());
    
    // Store data, count objects, analyze patterns, etc.
}
```

## Class Methods

### Public Methods

- `bool initialize()` - Initializes camera, WiFi, and WebSocket connection
- `void run()` - Main processing loop (call in Arduino `loop()`)
- `void setResponseCallback(ResponseCallback callback)` - Set custom response handler
- `void processFrame()` - Manually trigger frame processing
- `bool isSetupComplete()` - Check if WebSocket setup is complete

### Callback Function Type

```cpp
typedef void (*ResponseCallback)(const String& response);
```

Your callback function should match this signature:
```cpp
void myCallback(const String& response) {
    // Process the response here
}
```

## Configuration

The class uses these configurable constants:

- `FRAME_INTERVAL`: Time between frame captures (default: 500ms)
- `MAX_FRAME_SIZE`: Maximum frame size in bytes (default: 50,000)

You can modify these in the `vision_assistant.cpp` file if needed.

## Integration with Existing Code

To integrate with your existing project:

1. Add `vision_assistant.h` and `vision_assistant.cpp` to your project
2. Replace your existing `main.cpp` with one that uses the `VisionAssistant` class
3. Set up your custom response handlers as needed

## Error Handling

The class provides robust error handling:

- Camera initialization failures
- WiFi connection failures
- WebSocket connection issues
- Frame size validation
- JSON parsing errors

All errors are logged to Serial for debugging.

## Migration from Original Code

If you're migrating from the original `main.cpp`:

**Before:**
```cpp
// Original code had everything in main.cpp and websocket_callbacks.cpp
// Manual handling of WebSocket events
// Direct camera and WiFi management
```

**After:**
```cpp
VisionAssistant visionAssistant;

void myResponseHandler(const String& response) {
    // Your custom logic here
}

void setup() {
    visionAssistant.initialize();
    visionAssistant.setResponseCallback(myResponseHandler);
}

void loop() {
    visionAssistant.run();
}
```

## Example Files

- `main_with_vision_assistant.cpp` - Basic usage example
- `example_usage.cpp` - Advanced usage with different handler types

Choose the approach that best fits your needs!
