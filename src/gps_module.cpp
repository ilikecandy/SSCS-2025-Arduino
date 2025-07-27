#include "gps_module.h"

GPSModule::GPSModule() : gpsSerial(2) {
    currentData.isValid = false;
    currentData.latitude = 0.0;
    currentData.longitude = 0.0;
    currentData.altitude = 0.0;
    currentData.speed = 0.0;
    currentData.satellites = 0;
    currentData.hdop = 0.0;
    currentData.timestamp = 0;
    currentData.dateTime = "";
    lastValidFix = 0;
}

GPSModule::~GPSModule() {
    gpsSerial.end();
}

bool GPSModule::initialize() {
    Serial.println("Initializing GPS module...");
    Serial.printf("Using GPS_RX_PIN: %d (ESP32 RX, connects to GPS TX)\n", GPS_RX_PIN);
    Serial.printf("Using GPS_TX_PIN: %d (ESP32 TX, connects to GPS RX)\n", GPS_TX_PIN);
    
    // Start Serial 2 with the defined RX and TX pins and a baud rate of 9600
    gpsSerial.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    Serial.println("GPS Serial started at 9600 baud rate");
    
    // Wait longer for GPS to initialize
    Serial.println("Waiting for GPS module to stabilize...");
    delay(2000);
    
    // Test communication by checking for incoming data with more detailed logging
    Serial.println("Testing GPS communication (waiting up to 10 seconds)...");
    unsigned long startTime = millis();
    bool dataReceived = false;
    int bytesReceived = 0;
    
    while (millis() - startTime < 10000) { // Wait up to 10 seconds
        if (gpsSerial.available()) {
            if (!dataReceived) {
                Serial.println("✅ First GPS data received!");
                dataReceived = true;
            }
            
            // Read and display some raw data for debugging
            char c = gpsSerial.read();
            bytesReceived++;
            
            // Print first 100 characters for debugging
            if (bytesReceived <= 100) {
                Serial.print(c);
            } else if (bytesReceived == 101) {
                Serial.println("\n[More data available but not shown]");
            }
        }
        delay(50);
    }
    
    Serial.printf("\nTotal bytes received: %d\n", bytesReceived);
    
    if (dataReceived) {
        Serial.println("✅ GPS module communication established");
        return true;
    } else {
        Serial.println("❌ GPS module communication failed - no data received");
        Serial.println("\nTroubleshooting tips:");
        Serial.println("1. Check wiring:");
        Serial.println("   GPS VCC -> ESP32 3.3V (NOT 5V!)");
        Serial.println("   GPS GND -> ESP32 GND");
        Serial.printf("   GPS TX  -> ESP32 GPIO %d\n", GPS_RX_PIN);
        Serial.printf("   GPS RX  -> ESP32 GPIO %d\n", GPS_TX_PIN);
        Serial.println("2. Ensure GPS has clear view of sky");
        Serial.println("3. Check if GPS module LED is blinking");
        Serial.println("4. Try different baud rate (some modules use 4800 or 38400)");
        return false;
    }
}

void GPSModule::update() {
    // Read available data from GPS and encode it
    while (gpsSerial.available() > 0) {
        if (gps.encode(gpsSerial.read())) {
            // Check if location data was updated
            if (gps.location.isUpdated()) {
                currentData.isValid = gps.location.isValid();
                currentData.latitude = gps.location.lat();
                currentData.longitude = gps.location.lng();
                currentData.timestamp = millis();
                lastValidFix = millis();
            }
            
            // Update altitude if available
            if (gps.altitude.isUpdated()) {
                currentData.altitude = gps.altitude.meters();
            }
            
            // Update speed if available
            if (gps.speed.isUpdated()) {
                currentData.speed = gps.speed.kmph();
            }
            
            // Update satellite count if available
            if (gps.satellites.isUpdated()) {
                currentData.satellites = gps.satellites.value();
            }
            
            // Update HDOP if available
            if (gps.hdop.isUpdated()) {
                currentData.hdop = gps.hdop.value() / 100.0;
            }
            
            // Update date/time if available
            if (gps.date.isUpdated() && gps.time.isUpdated()) {
                currentData.dateTime = String(gps.date.year()) + "/" + 
                                     String(gps.date.month()) + "/" + 
                                     String(gps.date.day()) + "," + 
                                     String(gps.time.hour()) + ":" + 
                                     String(gps.time.minute()) + ":" + 
                                     String(gps.time.second());
            }
        }
    }
}

GPSData GPSModule::getGPSData() const {
    return currentData;
}

bool GPSModule::hasValidFix() const {
    return currentData.isValid && isRecentFix();
}

bool GPSModule::isRecentFix(unsigned long maxAge) const {
    return currentData.isValid && (millis() - currentData.timestamp) < maxAge;
}

String GPSModule::getLocationString() const {
    if (!hasValidFix()) {
        return "GPS: No fix";
    }
    
    return "GPS: " + String(currentData.latitude, 6) + ", " + String(currentData.longitude, 6) + 
           " (Alt: " + String(currentData.altitude, 1) + "m, Sats: " + String(currentData.satellites) + 
           ", HDOP: " + String(currentData.hdop, 2) + ")";
}

String GPSModule::getLocationJSON() const {
    String json = "{";
    json += "\"gps_valid\":" + String(hasValidFix() ? "true" : "false") + ",";
    
    if (hasValidFix()) {
        json += "\"latitude\":" + String(currentData.latitude, 6) + ",";
        json += "\"longitude\":" + String(currentData.longitude, 6) + ",";
        json += "\"altitude\":" + String(currentData.altitude, 1) + ",";
        json += "\"speed\":" + String(currentData.speed, 1) + ",";
        json += "\"satellites\":" + String(currentData.satellites) + ",";
        json += "\"hdop\":" + String(currentData.hdop, 2) + ",";
        json += "\"timestamp\":" + String(currentData.timestamp) + ",";
        json += "\"datetime\":\"" + currentData.dateTime + "\"";
    } else {
        json += "\"latitude\":null,";
        json += "\"longitude\":null,";
        json += "\"altitude\":null,";
        json += "\"speed\":null,";
        json += "\"satellites\":0,";
        json += "\"hdop\":null,";
        json += "\"timestamp\":null,";
        json += "\"datetime\":null";
    }
    
    json += "}";
    return json;
}

void GPSModule::printStatus() const {
    Serial.println("=== GPS Status ===");
    Serial.println("Valid fix: " + String(hasValidFix() ? "Yes" : "No"));
    
    if (currentData.isValid) {
        Serial.println("Latitude: " + String(currentData.latitude, 6));
        Serial.println("Longitude: " + String(currentData.longitude, 6));
        Serial.println("Altitude: " + String(currentData.altitude, 1) + " m");
        Serial.println("Speed: " + String(currentData.speed, 1) + " km/h");
        Serial.println("Satellites: " + String(currentData.satellites));
        Serial.println("HDOP: " + String(currentData.hdop, 2));
        Serial.println("Date/Time (UTC): " + currentData.dateTime);
        Serial.println("Last fix: " + String((millis() - currentData.timestamp) / 1000) + " seconds ago");
    } else {
        Serial.println("No valid GPS data available");
        if (lastValidFix > 0) {
            Serial.println("Last valid fix: " + String((millis() - lastValidFix) / 1000) + " seconds ago");
        } else {
            Serial.println("No GPS fix obtained since startup");
        }
    }
    
    // Print TinyGPS++ statistics
    Serial.println("Characters processed: " + String(gps.charsProcessed()));
    Serial.println("Sentences with fix: " + String(gps.sentencesWithFix()));
    Serial.println("Failed checksum: " + String(gps.failedChecksum()));
    Serial.println("Passed checksum: " + String(gps.passedChecksum()));
    
    Serial.println("==================");
}

void GPSModule::printRawData(int seconds) {
    Serial.printf("=== Raw GPS Data for %d seconds ===\n", seconds);
    unsigned long startTime = millis();
    
    while (millis() - startTime < (seconds * 1000)) {
        while (gpsSerial.available() > 0) {
            char c = gpsSerial.read();
            Serial.print(c);
        }
        delay(10);
    }
    
    Serial.println("\n=== End Raw Data ===");
}

bool GPSModule::testBaudRates() {
    Serial.println("=== Testing Different Baud Rates ===");
    
    int baudRates[] = {4800, 9600, 19200, 38400, 57600, 115200};
    int numRates = sizeof(baudRates) / sizeof(baudRates[0]);
    
    for (int i = 0; i < numRates; i++) {
        Serial.printf("Testing baud rate: %d\n", baudRates[i]);
        
        gpsSerial.end();
        delay(100);
        gpsSerial.begin(baudRates[i], SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
        delay(1000);
        
        // Test for 3 seconds
        unsigned long startTime = millis();
        bool dataReceived = false;
        int bytesReceived = 0;
        
        while (millis() - startTime < 3000) {
            if (gpsSerial.available()) {
                char c = gpsSerial.read();
                bytesReceived++;
                
                // Look for NMEA sentence start
                if (c == '$' && !dataReceived) {
                    dataReceived = true;
                    Serial.printf("✅ Data found at %d baud! ", baudRates[i]);
                }
                
                // Print first few characters
                if (bytesReceived <= 50) {
                    Serial.print(c);
                }
            }
            delay(10);
        }
        
        if (dataReceived) {
            Serial.printf("\n✅ Success! GPS responds at %d baud\n", baudRates[i]);
            Serial.printf("Bytes received: %d\n", bytesReceived);
            Serial.println("=====================================");
            return true;
        } else {
            Serial.printf("❌ No response at %d baud\n", baudRates[i]);
        }
    }
    
    Serial.println("❌ No GPS response found at any baud rate");
    Serial.println("Check wiring and power connections");
    Serial.println("=====================================");
    
    // Restore default baud rate
    gpsSerial.end();
    delay(100);
    gpsSerial.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    
    return false;
}

bool GPSModule::isConnected() const {
    // Check if we've received any data recently or have processed characters
    return (gps.charsProcessed() > 0) || (lastValidFix > 0) || (millis() - currentData.timestamp < GPS_TIMEOUT);
}
