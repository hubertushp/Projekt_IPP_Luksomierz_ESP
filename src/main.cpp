#include <Arduino.h>
#include <Wire.h>
#include <BH1750.h>
#include <DFRobot_BMI160.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_wifi.h"

// --- KONFIGURACJA PINÓW I2C ---
#define I2C_SDA_1 21 // SDA dla BMI160
#define I2C_SCL_1 22 // SCL dla BMI160
#define I2C_SDA_2 32 // SDA dla BH1750
#define I2C_SCL_2 33 // SCL dla BH1750
#define TEMP_PIN  25 // Pin do odczytu temperatury 

// --- KONFIGURACJA PINÓW ADDR DLA BH1750 ---
const int ADDR_PINS[4] = {5, 17, 16, 4}; 

// --- KONFIGURACJA HOTSPOTU ---
const char* ap_ssid = "ESP32_LUK_PROJEKT";
const char* ap_pass = "politechnika";

// --- OBIEKTY CZUJNIKÓW ---
const int BMI160_ADDR = 0x69; 
BH1750 luxSensor(0x23); 
DFRobot_BMI160 bmi160;

// --- ZMIENNE CZASOWE I FILTRU ---
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 500; // Interwał wysyłania (500ms)
float lux[4] = {0.0, 0.0, 0.0, 0.0};

// Zmienne filtru dolnoprzepustowego (EMA)
float filteredPitch = 0.0;
float filteredRoll = 0.0;
const float alpha = 1.0; // Współczynnik wygładzania szumu (zakres 0.01 - 1.0)

// Funkcja pomocnicza do pobierania IP klienta
String getTargetIP() {
    wifi_sta_list_t stationList;
    esp_wifi_ap_get_sta_list(&stationList);
    
    if (stationList.num > 0) {
        tcpip_adapter_sta_list_t adapterList;
        tcpip_adapter_get_sta_list(&stationList, &adapterList);
        return String(ip4addr_ntoa((ip4_addr_t*)&(adapterList.sta[0].ip)));
    }
    return "";
}

void setup() {
    Serial.begin(115200);

    WiFi.softAP(ap_ssid, ap_pass);
    Serial.println("Hotspot uruchomiony!");

    for (int i = 0; i < 4; i++) {
        pinMode(ADDR_PINS[i], OUTPUT);
        digitalWrite(ADDR_PINS[i], HIGH); 
    }

    Wire.begin(I2C_SDA_1, I2C_SCL_1);   // Magistrala 1 (BMI160)
    Wire1.begin(I2C_SDA_2, I2C_SCL_2); // Magistrala 2 (BH1750)

    if (bmi160.softReset() != BMI160_OK) Serial.println("Błąd resetu BMI160");
    if (bmi160.I2cInit(BMI160_ADDR) != BMI160_OK) Serial.println("Błąd inicjalizacji BMI160");

    for (int i = 0; i < 4; i++) {
        digitalWrite(ADDR_PINS[i], LOW); 
        delay(10); 
        
        if(!luxSensor.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &Wire1)) {
            Serial.printf("Błąd inicjalizacji BH1750 na pinie ADDR: %d\n", ADDR_PINS[i]);
        }
        
        digitalWrite(ADDR_PINS[i], HIGH); 
        delay(10);
    }

    Serial.println("System gotowy.");
}

void loop() {
    if (millis() - lastSendTime >= sendInterval) {
        lastSendTime = millis();
        String targetIP = getTargetIP();

        // 1. ODCZYT CZUJNIKÓW ŚWIATŁA
        for (int i = 0; i < 4; i++) {
            digitalWrite(ADDR_PINS[i], LOW); 
            delay(5); 
            lux[i] = luxSensor.readLightLevel();
            digitalWrite(ADDR_PINS[i], HIGH); 
        }

        if (targetIP != "") {
            // 2. ODCZYT I OBLICZENIA BMI160
            int16_t accelGyro[6] = {0};
            bmi160.getAccelGyroData(accelGyro);
            
            // Przeliczenie na wartość przyśpieszenia (rozdzielczość 16384 LSB/g)
            float accelX = accelGyro[3] / 16384.0; 
            float accelY = accelGyro[4] / 16384.0;
            float accelZ = accelGyro[5] / 16384.0;

            // Obliczenia kątów
            float rawPitch = atan2(-accelX, sqrt(accelY * accelY + accelZ * accelZ)) * 180.0 / PI;
            float rawRoll = atan2(accelY, accelZ) * 180.0 / PI;

            // Aplikacja filtru EMA
            filteredPitch = (alpha * rawPitch) + ((1.0 - alpha) * filteredPitch);
            filteredRoll = (alpha * rawRoll) + ((1.0 - alpha) * filteredRoll);

            float tempC = (analogRead(TEMP_PIN) * 3300.0 / 4096.0) / 100.0;

            // 3. TWORZENIE I WYSYŁANIE JSON
            JsonDocument doc; 
            doc["lux_1"] = lux[0];
            doc["lux_2"] = lux[1];
            doc["lux_3"] = lux[2];
            doc["lux_4"] = lux[3];
            doc["pitch"] = filteredPitch; // Użycie wygładzonych danych
            doc["roll"]  = filteredRoll;  // Użycie wygładzonych danych
            doc["temp"]  = tempC;

            String jsonPayload;
            serializeJson(doc, jsonPayload);

            HTTPClient http;
            String url = "http://" + targetIP + ":8000/data";
            
            http.begin(url);
            http.addHeader("Content-Type", "application/json");

            Serial.println("--- WYSYŁANA RAMKA JSON ---");
            Serial.println(jsonPayload);
            
            int httpCode = http.POST(jsonPayload);
            if (httpCode > 0) Serial.printf("Sukces (POST): %d\n", httpCode);
            else Serial.printf("Błąd (POST): %s\n", http.errorToString(httpCode).c_str());
            
            http.end();
        } else {
            Serial.println("Czekam na połączenie klienta...");
        }
    }
}