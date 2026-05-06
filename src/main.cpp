#include <Arduino.h>
#include <Wire.h>
#include <BH1750.h>
#include <DFRobot_BMI160.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_wifi.h"

// --- KONFIGURACJA PINÓW ---
#define I2C_SDA_1 21
#define I2C_SCL_1 22
#define I2C_SDA_2 18
#define I2C_SCL_2 19
#define TEMP_PIN  34

// --- KONFIGURACJA HOTSPOTU ---
const char* ap_ssid = "ESP32_LUK_PROJEKT";
const char* ap_pass = "politechnika";

// --- OBIEKTY CZUJNIKÓW ---
BH1750 lux1, lux2, lux3, lux4;
DFRobot_BMI160 bmi160;
const int BMI160_ADDR = 0x69; // Adres I2C dla BMI160

// --- FUNKCJA POMOCNICZA DO POBIERANIA IP KLIENTA ---
String getTargetIP() {
    wifi_sta_list_t stationList;
    esp_wifi_ap_get_sta_list(&stationList);
    
    if (stationList.num > 0) {
        tcpip_adapter_sta_list_t adapterList;
        tcpip_adapter_get_sta_list(&stationList, &adapterList);
        // Dodano rzutowanie (ip4_addr_t*), aby naprawić błąd kompilacji
        return String(ip4addr_ntoa((ip4_addr_t*)&(adapterList.sta[0].ip)));
    }
    return "";
}

void setup() {
    Serial.begin(115200);

    WiFi.softAP(ap_ssid, ap_pass);
    Serial.println("Hotspot uruchomiony!");

    Wire.begin(I2C_SDA_1, I2C_SCL_1);   
    Wire1.begin(I2C_SDA_2, I2C_SCL_2); 

    if (bmi160.softReset() != BMI160_OK) Serial.println("BMI160 reset fail");
    if (bmi160.I2cInit(BMI160_ADDR) != BMI160_OK) Serial.println("BMI160 init fail");

    lux1.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &Wire);
    lux2.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x5C, &Wire);
    lux3.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &Wire1);
    lux4.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x5C, &Wire1);

    Serial.println("System gotowy.");
}

void loop() {
    String targetIP = getTargetIP();

    if (targetIP != "") {
        int16_t accelGyro[6] = {0};
        bmi160.getAccelGyroData(accelGyro);
        
        float pitch = accelGyro[3] / 131.0;
        float roll  = accelGyro[4] / 131.0;
        //float tempC = 25.0;
        float tempC = (analogRead(TEMP_PIN) * 3.3 / 4095.0) * 100.0;

        // Twozymy JSON z danymi
        JsonDocument doc; 
        doc["lux_1"] = lux1.readLightLevel();
        //doc["lux_2"] = 0.0;
        //doc["lux_3"] = 0.0;
        //doc["lux_4"] = 0.0;
        doc["lux_2"] = lux2.readLightLevel();
        doc["lux_3"] = lux3.readLightLevel();
        doc["lux_4"] = lux4.readLightLevel();
        doc["pitch"] = pitch;
        doc["roll"]  = roll;
        doc["temp"]  = tempC;

        String jsonPayload;
        serializeJson(doc, jsonPayload);

        HTTPClient http;
        String url = "http://" + targetIP + ":8000/data";
        
        http.begin(url);
        http.addHeader("Content-Type", "application/json");

        Serial.println("--- WYSYŁANA RAMKA JSON ---");
        Serial.println(jsonPayload); 
        Serial.println("---------------------------");
        
        int httpCode = http.POST(jsonPayload);
        if (httpCode > 0) Serial.printf("Sukces: %d\n", httpCode);
        else Serial.printf("Błąd: %s\n", http.errorToString(httpCode).c_str());
        
        http.end();
    } else {
        Serial.println("Czekam na klienta...");
    }

    delay(500);
}