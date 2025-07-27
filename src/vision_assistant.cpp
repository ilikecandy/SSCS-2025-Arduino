#include "vision_assistant.h"

#include <WiFi.h>

#include "base64.h"
#include "camera_pins.h"
#include "camera_setup.h"
#include "gemini_config.h"
#include "secrets.h"

VisionAssistant *VisionAssistant::instance = nullptr;

VisionAssistant::VisionAssistant() : setupComplete(false), systemPromptSent(false), lastFrameTime(0), lastGPSUpdate(0), responseCallback(nullptr), toolCallback(nullptr) {
    instance = this;  // Set static instance for callbacks
}

VisionAssistant::~VisionAssistant() {
    instance = nullptr;
}

bool VisionAssistant::initialize() {
    Serial.begin(115200);
    Serial.println("Initializing Vision Assistant...");

    // Initialize camera
    if (!initializeCamera()) {
        Serial.println("Failed to initialize camera");
        return false;
    }

    // Initialize GPS
    if (!initializeGPS()) {
        Serial.println("Warning: Failed to initialize GPS - continuing without GPS");
        // Don't return false here, GPS is optional
    }

    // Connect to WiFi
    if (!connectToWiFi()) {
        Serial.println("Failed to connect to WiFi");
        return false;
    }

    // Initialize WebSocket
    if (!initializeWebSocket()) {
        Serial.println("Failed to initialize WebSocket");
        return false;
    }

    // Set default response callback
    setResponseCallback(onGeminiResponse);

    Serial.println("Vision Assistant initialized successfully!");
    return true;
}

void VisionAssistant::run() {
    // Handle WebSocket communication
    ws.loop();

    // Update GPS data
    unsigned long currentTime = millis();
    if (currentTime - lastGPSUpdate >= GPS_UPDATE_INTERVAL) {
        gps.update();
        lastGPSUpdate = currentTime;
    }

    // Check if it's time to process a new frame
    if (currentTime - lastFrameTime >= FRAME_INTERVAL) {
        processFrame();
        lastFrameTime = currentTime;
    }
}

void VisionAssistant::setResponseCallback(ResponseCallback callback) {
    responseCallback = callback;
}

void VisionAssistant::setToolCallback(ToolCallback callback) {
    toolCallback = callback;
}

void VisionAssistant::processFrame() {
    // Wait until WebSocket is connected and setup is complete
    if (!setupComplete) {
        Serial.println(ws.isConnected() ? "WebSocket connected but setupComplete is false" : "WebSocket not connected and setupComplete is false");
        return;
    }

    // Capture frame
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        return;
    }

    // Check frame size
    if (fb->len > MAX_FRAME_SIZE) {
        Serial.printf("Frame too large (%zu bytes), skipping\n", fb->len);
        esp_camera_fb_return(fb);
        return;
    }

    // Encode and send frame with GPS data
    String frameB64 = base64_encode(fb->buf, fb->len);
    Serial.printf("Frame captured: %zu bytes, Base64 length: %d\n", fb->len, frameB64.length());

    // Get GPS data
    GPSData gpsData = gps.getGPSData();
    String gpsText = "";
    
    if (gpsData.isValid && gps.hasValidFix()) {
        gpsText = "Current GPS location: Latitude " + String(gpsData.latitude, 6) + 
                 ", Longitude " + String(gpsData.longitude, 6) + 
                 ", Altitude " + String(gpsData.altitude, 1) + "m. ";
        Serial.printf("Including GPS data: %s\n", gpsText.c_str());
    } else {
        gpsText = "GPS location not available. ";
        Serial.println("GPS data not available or not recent");
    }

    // Create message with both image and GPS context
    String msg = "{\"client_content\":{\"turn_complete\":true,\"turns\":[{\"role\":\"user\",\"parts\":[";
    
    // Add GPS text if available
    if (gpsText.length() > 0) {
        msg += "{\"text\":\"" + gpsText + "\"},";
    }
    
    // Add image data
    msg += "{\"inline_data\":{\"mime_type\":\"image/jpeg\",\"data\":\"" + frameB64 + "\"}}";
    msg += "]}]}}";

    bool sent = ws.sendTXT(msg);
    if (!sent) {
        Serial.println("Failed to send frame to Gemini");
    }

    esp_camera_fb_return(fb);
}

bool VisionAssistant::isSetupComplete() const {
    return setupComplete;
}

bool VisionAssistant::connectToWiFi() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nFailed to connect to WiFi");
        return false;
    }

    Serial.println("\nWiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    return true;
}

bool VisionAssistant::initializeCamera() {
    setupCamera();
    return true;  // Assuming setupCamera() handles its own error checking
}

bool VisionAssistant::initializeWebSocket() {
    ws.beginSSL(WS_HOST, WS_PORT, WS_PATH.c_str());
    ws.onEvent(webSocketEvent);
    ws.setReconnectInterval(5000);
    return true;
}

bool VisionAssistant::initializeGPS() {
    Serial.println("Initializing GPS module...");
    
    if (!gps.initialize()) {
        Serial.println("GPS initialization failed");
        return false;
    }
    
    Serial.println("GPS module initialized successfully");
    return true;
}

void VisionAssistant::sendSetupMessage() {
    // NOTE: This should be set to low only when testing.
    // const char* mediaResolution = "MEDIA_RESOLUTION_HIGH";
    const char* mediaResolution = "MEDIA_RESOLUTION_LOW";
    String setupMsg = "{\"setup\":{\"model\":\"models/gemini-2.5-flash-live-preview\",\"generationConfig\":{\"responseModalities\":[\"TEXT\"], \"mediaResolution\":\"" + String(mediaResolution) + "\"},\"tools\":[" + String(TOOLS_JSON) + "],\"systemInstruction\":{\"parts\":[{\"text\":\"" + String(SYSTEM_PROMPT) + "\"}]}}}";
    ws.sendTXT(setupMsg);
    Serial.println("Sent setup message");
}

void VisionAssistant::sendToolResponse(const char *functionId, const char *functionName, const char *result) {
    // Create tool response according to BidiGenerateContentToolResponse format
    String toolResponseMsg = "{\"toolResponse\":{\"functionResponses\":[{";

    if (functionId) {
        toolResponseMsg += "\"id\":\"" + String(functionId) + "\",";
    }

    toolResponseMsg += "\"name\":\"" + String(functionName) + "\",";
    toolResponseMsg += "\"response\":{\"output\":\"" + String(result) + "\"}";
    toolResponseMsg += "}]}}";

    bool sent = ws.sendTXT(toolResponseMsg);
    if (sent) {
        Serial.printf("Sent tool response for %s\n", functionName);
    } else {
        Serial.printf("Failed to send tool response for %s\n", functionName);
    }
}

void VisionAssistant::handleWebSocketMessage(const JsonDocument &doc) {
    Serial.println("Received WebSocket message:");
    serializeJsonPretty(doc, Serial);
    Serial.println();
    // Check for setupComplete signal
    if (doc.containsKey("setupComplete")) {
        Serial.println("Setup complete – ready to send frames");
        setupComplete = true;
        systemPromptSent = true;
        return;
    }

    // serializeJsonPretty(doc, Serial);
    // Serial.println();
    // Handle toolCall at top level
    if (doc.containsKey("toolCall")) {
        JsonObjectConst toolCall = doc["toolCall"];
        if (toolCall.containsKey("functionCalls")) {
            Serial.println("Function calls detected");
            JsonArrayConst functionCalls = toolCall["functionCalls"];
            for (JsonObjectConst funcCall : functionCalls) {
                const char *toolName = funcCall["name"];
                const char *toolId = funcCall["id"];
                Serial.printf("Tool call detected: %s (ID: %s)\n", toolName, toolId ? toolId : "N/A");
                if (toolName && strcmp(toolName, "speakMessage") == 0) {
                    const char *message = funcCall["args"]["message"];
                    if (message && toolCallback) {
                        Serial.printf("Tool call: %s(\"%s\")\n", toolName, message);
                        toolCallback(toolName, message);

                        // Send tool response back to Gemini
                        sendToolResponse(toolId, toolName, "Function executed successfully");
                    }
                }
            }
        } else {
            Serial.println("No function calls found in toolCall");
        }
        return;
    }

    // Handle model text response
    if (doc.containsKey("serverContent") && doc["serverContent"].containsKey("modelTurn")) {
        JsonObjectConst modelTurn = doc["serverContent"]["modelTurn"];
        if (modelTurn["parts"][0].containsKey("text")) {
            const char *text = modelTurn["parts"][0]["text"];
            if (text && responseCallback) {
                String response = String(text);
                Serial.printf("Gemini: %s\n", text);
                responseCallback(response);
            }
        } else {
            Serial.println("No text in modelTurn response");
        }
    } else {
        // TODO: DETECT TURNS
        // Serial.println("No serverContent or modelTurn in response");
        // serializeJsonPretty(doc, Serial);
        // Serial.println();
    }
}

void VisionAssistant::webSocketEvent(WStype_t type, uint8_t *payload, size_t length) {
    if (!instance)
        return;

    switch (type) {
        case WStype_DISCONNECTED:
            Serial.println("[WSc] Disconnected!");
            instance->setupComplete = false;
            instance->systemPromptSent = false;
            break;

        case WStype_CONNECTED: {
            Serial.printf("[WSc] Connected to url: %s\n", (char *)payload);
            instance->setupComplete = false;
            instance->systemPromptSent = false;
            instance->sendSetupMessage();
            break;
        }

        case WStype_BIN: {
            JsonDocument doc;
            auto error = deserializeJson(doc, payload, length);
            if (error) {
                Serial.print("deserializeJson() failed: ");
                Serial.println(error.c_str());
                // Print the raw payload to see what we received
                Serial.print("Raw payload: ");
                Serial.write(payload, length);
                Serial.println();
                return;
            }

            instance->handleWebSocketMessage(doc);
            break;
        }

        default:
            break;
    }
}

GPSData VisionAssistant::getCurrentGPSData() const {
    return gps.getGPSData();
}

String VisionAssistant::getGPSString() const {
    return gps.getLocationString();
}

void VisionAssistant::onGeminiResponse(const String &response) {
    // This is the default callback for Gemini text responses
    // Users can set their own callback using setResponseCallback()
    // For now, just log it (already logged in handleWebSocketMessage)
    // You could add additional processing here like:
    // - Parse object detection results
    // - Trigger actions based on detected objects
    // - Store responses for analysis
    // - Send responses to other systems
    Serial.println("Default response handler - response already processed");
}
