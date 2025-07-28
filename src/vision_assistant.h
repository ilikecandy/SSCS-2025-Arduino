#ifndef VISION_ASSISTANT_H
#define VISION_ASSISTANT_H

#include <Arduino.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "esp_camera.h"
#include "gps_module.h"

// Forward declarations
class VisionAssistant;

// Callback function types
typedef void (*ResponseCallback)(const String& response);
typedef void (*ToolCallback)(const String& toolName, const String& message);

class VisionAssistant {
private:
    static VisionAssistant* instance;
    
    WebSocketsClient ws;
    GPSModule gps;
    bool setupComplete;
    bool systemPromptSent;
    unsigned long lastFrameTime;
    unsigned long lastGPSUpdate;
    ResponseCallback responseCallback;
    ToolCallback toolCallback;
    
    // Frame processing constants
    static const unsigned long FRAME_INTERVAL = 5000; // 500ms between frames
    static const unsigned long GPS_UPDATE_INTERVAL = 1000; // 1 second between GPS updates
    static const size_t MAX_FRAME_SIZE = 50000; // Maximum frame size in bytes
    
public:
    VisionAssistant();
    ~VisionAssistant();
    
    // Initialization and main loop
    bool initialize();
    void run();
    
    // Callback management
    void setResponseCallback(ResponseCallback callback);
    void setToolCallback(ToolCallback callback);
    
    // Frame processing
    void processFrame();
    bool isSetupComplete() const;
    
    // GPS access
    GPSData getCurrentGPSData() const;
    String getGPSString() const;

    // Send a text message to Gemini
    void sendTextMessage(const String& message);
    
    // Static callback for WebSocket events
    static void webSocketEvent(WStype_t type, uint8_t *payload, size_t length);
    
    // Response handling
    static void onGeminiResponse(const String& response);
    
private:
    // Internal helper methods
    bool connectToWiFi();
    bool initializeCamera();
    bool initializeGPS();
    bool initializeWebSocket();
    void sendSetupMessage();
    void sendToolResponse(const char* functionId, const char* functionName, const char* result);
    void handleWebSocketMessage(const JsonDocument& doc);
};

#endif // VISION_ASSISTANT_H
