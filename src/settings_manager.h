#ifndef SETTINGS_MANAGER_H
#define SETTINGS_MANAGER_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

struct UserSettings {
    String language;
    bool isValid;

    UserSettings() : language("en-US"), isValid(false) {}
};

class SettingsManager {
private:
    String notificationsApiUrl;
    UserSettings currentSettings;
    unsigned long lastFetchTime;
    static const unsigned long CACHE_DURATION = 300000; // 5 minutes cache

public:
    SettingsManager(const String& apiUrl);

    // Fetch settings from the API
    bool fetchSettings();

    // Get current settings (fetches if cache expired)
    UserSettings getSettings();

    // Get language setting specifically
    String getLanguage();

    // Force refresh settings
    bool refreshSettings();
};

#endif
