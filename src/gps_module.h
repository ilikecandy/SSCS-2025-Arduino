#ifndef GPS_MODULE_H
#define GPS_MODULE_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include <TinyGPS++.h>

struct GPSData {
    bool isValid;
    float latitude;
    float longitude;
    float altitude;
    float speed;
    int satellites;
    float hdop;
    unsigned long timestamp;
    String dateTime;
};

class GPSModule {
private:
    // GPS pins - based on ESP32 UART2 default pins but using 32/33
    // Tutorial uses GPIO 16/17 for UART2, but we're using 32/33 to avoid conflicts
    static const int GPS_RX_PIN = 32;  // Connect to GPS TX
    static const int GPS_TX_PIN = 33;  // Connect to GPS RX
    static const int GPS_BAUD_RATE = 9600;
    
    TinyGPSPlus gps;
    HardwareSerial gpsSerial;
    GPSData currentData;
    unsigned long lastValidFix;
    static const unsigned long GPS_TIMEOUT = 30000; // 30 seconds
    
public:
    GPSModule();
    ~GPSModule();
    
    // Initialization
    bool initialize();
    
    // Main update function - call this regularly in loop
    void update();
    
    // Data access
    GPSData getGPSData() const;
    bool hasValidFix() const;
    bool isRecentFix(unsigned long maxAge = 10000) const; // Default 10 seconds
    
    // Formatted strings for easy display/transmission
    String getLocationString() const;
    String getLocationJSON() const;
    
    // Status information
    void printStatus() const;
    void printRawData(int seconds = 10);  // Print raw NMEA data for debugging (removed const)
    bool testBaudRates();  // Test different baud rates
    bool isConnected() const;
};

#endif // GPS_MODULE_H
