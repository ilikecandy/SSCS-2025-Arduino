#include "esp_camera.h"
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "secrets.h"

// ---- Camera Configuration (Freenove WROVER CAM) ----
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     21
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       19
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM        5
#define Y2_GPIO_NUM        4
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ---- Gemini Live API Configuration ----
WebSocketsClient ws;
const char* ws_host = "generativelanguage.googleapis.com";
const int   ws_port = 443;
String ws_path = "/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent?key=" + String(GEMINI_API_KEY);

// ---- Base64 Encoding Alphabet ----
const char b64_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// ---- System Prompt ----
const char* SYSTEM_PROMPT = "You are a vision assistant that analyzes camera frames. Be very brief in your responses, describing what you see in just a few words.";

// ---- Globals ----
bool setupComplete = false;
bool systemPromptSent = false;

// ---- Base64 Encode Function ----
String base64_encode(const uint8_t *data, size_t len) {
    String encoded;
    encoded.reserve(((len + 2) / 3) * 4);
    int pad = len % 3;

    for (size_t i = 0; i < len; i += 3) {
        uint32_t val = 0;
        for (int j = 0; j < 3; ++j) {
            val <<= 8;
            if (i + j < len) {
                val |= data[i + j];
            }
        }
        encoded += b64_alphabet[(val >> 18) & 0x3F];
        encoded += b64_alphabet[(val >> 12) & 0x3F];
        encoded += (i + 1 < len) ? b64_alphabet[(val >> 6) & 0x3F] : '=';
        encoded += (i + 2 < len) ? b64_alphabet[val & 0x3F] : '=';
    }

    // Correct padding
    if (pad == 1) {
        encoded.setCharAt(encoded.length() - 1, '=');
    }
    return encoded;
}

// ---- WebSocket Event Handler ----
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            Serial.println("[WSc] Disconnected!");
            setupComplete = false;
            systemPromptSent = false;
            break;

        case WStype_CONNECTED: {
            Serial.printf("[WSc] Connected to url: %s\n", (char *)payload);
            setupComplete = false;
            systemPromptSent = false;
            // Send setup message with system instruction
            String setupMsg = "{\"setup\":{\"model\":\"models/gemini-2.5-flash-live-preview\",\"generationConfig\":{\"responseModalities\":[\"TEXT\"]},\"systemInstruction\":{\"parts\":[{\"text\":\"" + String(SYSTEM_PROMPT) + "\"}]}}}";
            ws.sendTXT(setupMsg);
            Serial.println("Sent setup message");
            break;
        }

        case WStype_BIN: {
            // The response is binary, but it's a JSON string.
            // So we parse it as JSON.
            DynamicJsonDocument doc(2048); // Increased size for potentially larger payloads
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

            // Check for setupComplete signal
            if (doc.containsKey("setupComplete")) {
                Serial.println("Setup complete – ready to send frames");
                setupComplete = true;
                systemPromptSent = true;
                return;
            }

            // Handle model text response
            if (doc.containsKey("serverContent") && doc["serverContent"].containsKey("modelTurn")) {
                const char* text = doc["serverContent"]["modelTurn"]["parts"][0]["text"];
                if (text) {
                    Serial.printf("Gemini: %s\n", text);
                } else {
                    Serial.println("No text in modelTurn response");
                }
            } else {
                 Serial.println("No serverContent or modelTurn in response");
                 serializeJsonPretty(doc, Serial);
                 Serial.println();
            }
            break;
        }

        default:
            break;
    }
}

// ---- Setup Function ----
void setup() {
    Serial.begin(115200);

    // Initialize camera
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = Y2_GPIO_NUM;
    config.pin_d1       = Y3_GPIO_NUM;
    config.pin_d2       = Y4_GPIO_NUM;
    config.pin_d3       = Y5_GPIO_NUM;
    config.pin_d4       = Y6_GPIO_NUM;
    config.pin_d5       = Y7_GPIO_NUM;
    config.pin_d6       = Y8_GPIO_NUM;
    config.pin_d7       = Y9_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_pclk     = PCLK_GPIO_NUM;
    config.pin_vsync    = VSYNC_GPIO_NUM;
    config.pin_href     = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode    = CAMERA_GRAB_LATEST;

    if (psramFound()) {
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 12;
        config.fb_count = 2;
        config.fb_location = CAMERA_FB_IN_PSRAM;
    } else {
        config.frame_size = FRAMESIZE_QVGA;
        config.jpeg_quality = 15;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_DRAM;
    }

    if (esp_camera_init(&config) != ESP_OK) {
        Serial.println("Camera init failed");
        return;
    }
    sensor_t * s = esp_camera_sensor_get();
    s->set_vflip(s, 1);

    // Connect to WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // Initialize WebSocket
    ws.beginSSL(ws_host, ws_port, ws_path.c_str());
    ws.onEvent(webSocketEvent);
    ws.setReconnectInterval(5000);
}

// ---- Main Loop ----
void loop() {
    ws.loop();

    // Wait until WebSocket is connected and setup is complete
    if (!setupComplete) {
        Serial.println("WebSocket not connected or setup not complete");
        Serial.println(ws.isConnected() ? "WebSocket connected" : "WebSocket not connected");
        return;
    }

    // Capture frame
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        return;
    }

    // Skip too-large frames
    if (fb->len > 50000) {
        Serial.println("Frame too large, skipping");
        esp_camera_fb_return(fb);
        return;
    }

    // Base64 encode and send as mediaChunks
    String frameB64 = base64_encode(fb->buf, fb->len);

    // Send only the image without text prompt (system instruction already sent during setup)
    String msg = "{\"client_content\":{\"turn_complete\":true,\"turns\":[{\"role\":\"user\",\"parts\":[{\"inline_data\":{\"mime_type\":\"image/jpeg\",\"data\":\"" + frameB64 + "\"}}]}]}}";

    ws.sendTXT(msg);
    // Serial.println("Sent frame with msg: " + msg);

    esp_camera_fb_return(fb);
    delay(500);  // Control frame rate (500ms)
}
