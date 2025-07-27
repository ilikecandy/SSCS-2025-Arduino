// /*
//  * GPS Only Test Main
//  * 
//  * Rename this file to main.cpp to test GPS functionality only
//  * Useful for debugging GPS issues without camera/WiFi complexity
//  */

// #include <Arduino.h>
// #include "gps_module.h"

// GPSModule gps;
// unsigned long lastStatusPrint = 0;
// unsigned long lastDataPrint = 0;

// void setup() {
//     Serial.begin(115200);
//     delay(1000);
    
//     Serial.println("========================================");
//     Serial.println("GPS Module Test - ESP32");
//     Serial.println("========================================");
//     Serial.println();
    
//     // Initialize GPS
//     Serial.println("Initializing GPS module...");
//     if (!gps.initialize()) {
//         Serial.println("❌ GPS initialization failed!");
//         Serial.println("Check connections:");
//         Serial.println("  GPS VCC -> ESP32 3.3V");
//         Serial.println("  GPS GND -> ESP32 GND");
//         Serial.println("  GPS TX  -> ESP32 GPIO 16");
//         Serial.println("  GPS RX  -> ESP32 GPIO 17");
//         while(true) {
//             delay(1000);
//         }
//     }
    
//     Serial.println("✅ GPS module initialized successfully!");
//     Serial.println("🛰️  Searching for satellites...");
//     Serial.println("📍 GPS fix may take 30-60 seconds outdoors");
//     Serial.println();
// }

// void loop() {
//     // Update GPS continuously
//     gps.update();
    
//     unsigned long currentTime = millis();
    
//     // Print detailed status every 10 seconds
//     if (currentTime - lastStatusPrint > 10000) {
//         Serial.println("\n" + String("=").substring(0, 50));
//         Serial.println("GPS Status Update - " + String(currentTime/1000) + "s");
//         Serial.println(String("=").substring(0, 50));
//         gps.printStatus();
//         lastStatusPrint = currentTime;
//     }
    
//     // Print GPS data every 2 seconds if we have a fix
//     if (currentTime - lastDataPrint > 2000) {
//         GPSData data = gps.getGPSData();
//         if (data.isValid && gps.hasValidFix()) {
//             Serial.println("🌍 " + gps.getLocationString());
//             Serial.println("📊 JSON: " + gps.getLocationJSON());
//             Serial.println();
//         } else {
//             Serial.println("🔍 Searching for GPS fix...");
//         }
//         lastDataPrint = currentTime;
//     }
    
//     // Small delay to prevent overwhelming serial output
//     delay(50);
// }
