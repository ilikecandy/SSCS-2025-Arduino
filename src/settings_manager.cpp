#include "settings_manager.h"
#include <WiFi.h>

SettingsManager::SettingsManager(const String& apiUrl) 
    : notificationsApiUrl(apiUrl), lastFetchTime(0) {
}

bool SettingsManager::fetchSettings() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ WiFi not connected - cannot fetch settings");
        return false;
    }
    
    HTTPClient http;
    String settingsUrl = notificationsApiUrl + "/settings?device_id=companion_app";
    
    Serial.printf("📡 Fetching settings from: %s\n", settingsUrl.c_str());
    
    if (http.begin(settingsUrl)) {
        http.addHeader("Content-Type", "application/json");
        http.setTimeout(10000); // 10 second timeout
        
        int httpCode = http.GET();
        Serial.printf("Settings API HTTP Response Code: %d\n", httpCode);
        
        if (httpCode == HTTP_CODE_OK) {
            String response = http.getString();
            Serial.println("Settings API Response:");
            Serial.println(response);
            
            // Parse JSON response
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, response);
            
            if (error) {
                Serial.printf("❌ Failed to parse settings JSON: %s\n", error.c_str());
                http.end();
                return false;
            }
            
            // Extract language setting
            if (doc.containsKey("language")) {
                currentSettings.language = doc["language"].as<String>();
                currentSettings.isValid = true;
                lastFetchTime = millis();
                
                Serial.printf("✅ Settings fetched successfully. Language: %s\n", currentSettings.language.c_str());
                http.end();
                return true;
            } else {
                Serial.println("❌ Language setting not found in response");
                
                // Print available keys for debugging
                Serial.println("Available keys in response:");
                JsonObject obj = doc.as<JsonObject>();
                for (JsonPair kv : obj) {
                    Serial.printf("  - %s\n", kv.key().c_str());
                }
            }
        } else {
            String error_response = http.getString();
            Serial.printf("❌ HTTP Error Code: %d\n", httpCode);
            Serial.println("Error Response: " + error_response);
        }
        
        http.end();
    } else {
        Serial.println("❌ Failed to begin HTTP connection to settings API");
    }
    
    return false;
}

UserSettings SettingsManager::getSettings() {
    // Check if cache is expired or invalid
    if (!currentSettings.isValid || (millis() - lastFetchTime > CACHE_DURATION)) {
        Serial.println("Settings cache expired or invalid, fetching new settings...");
        if (!fetchSettings()) {
            Serial.println("Failed to fetch settings, using defaults");
            // Return default settings if fetch fails
            UserSettings defaultSettings;
            defaultSettings.language = "en-US";
            defaultSettings.isValid = true;
            return defaultSettings;
        }
    }
    
    return currentSettings;
}

String SettingsManager::getLanguage() {
    UserSettings settings = getSettings();
    return settings.language;
}

bool SettingsManager::refreshSettings() {
    return fetchSettings();
}
