/**
 * @file ESP32_Parser_Production.ino
 * @brief FINAL PRODUCTION Parser for SR Method STM32 Data.
 * This version has all debug messages disabled for clean output to the host PC.
 */

#define STM32_SERIAL Serial2
#define RXD2 16
#define TXD2 17
#define BAUD_RATE 115200
#define PACKET_TIMEOUT_MS 3000

// State variables
float g_temperature = 0.0, g_waterLevelPercent = 0.0, g_vibrationValue = 0.0;
int   g_anomalyStatus = 0, g_pumpStatus = 0;
bool  g_receivedTemp = false, g_receivedWater = false, g_receivedAnomaly = false, g_receivedPump = false;
unsigned long g_lastReceiveTime = 0;

void reset_state() {
  g_receivedTemp = false; g_receivedWater = false; g_receivedAnomaly = false; g_receivedPump = false;
}

void setup() {
  Serial.begin(BAUD_RATE);
  STM32_SERIAL.begin(BAUD_RATE, SERIAL_8N1, RXD2, TXD2);
}

void loop() {
  if ((g_receivedTemp || g_receivedWater || g_receivedAnomaly || g_receivedPump) && (millis() - g_lastReceiveTime > PACKET_TIMEOUT_MS)) {
      reset_state();
  }

  if (STM32_SERIAL.available()) {
    String line = STM32_SERIAL.readStringUntil('\n');
    line.trim();

    if (line.length() == 0) return;

    bool processed_line = false;
    
    if (line.startsWith("NOMINAL") || line.startsWith("ANOMALY")) {
      g_anomalyStatus = line.startsWith("ANOMALY") ? 1 : 0;
      int commaIndex = line.indexOf(',');
      if (commaIndex != -1) g_vibrationValue = line.substring(commaIndex + 1).toFloat();
      g_receivedAnomaly = true; processed_line = true;
      
    } else if (line.startsWith("WaterLevel")) {
      int firstComma = line.indexOf(','); int secondComma = line.indexOf(',', firstComma + 1);
      if (secondComma != -1) g_waterLevelPercent = line.substring(secondComma + 1).toFloat();
      g_receivedWater = true; processed_line = true;

    } else if (line.startsWith("Pump")) {
      g_pumpStatus = (line.indexOf("ON") != -1) ? 1 : 0;
      g_receivedPump = true; processed_line = true;
      
    } else if (line.startsWith("WaterTemp")) {
      int commaIndex = line.indexOf(',');
      if (commaIndex != -1) g_temperature = line.substring(commaIndex + 1).toFloat();
      g_receivedTemp = true; processed_line = true;
    }
    
    if (processed_line) g_lastReceiveTime = millis();

    if (g_receivedTemp && g_receivedWater && g_receivedAnomaly && g_receivedPump) {
      char outputBuffer[128];
      sprintf(outputBuffer, "%.2f,%.2f,%d,%d,%.2f\n",
              g_temperature, g_waterLevelPercent, g_anomalyStatus, g_pumpStatus, g_vibrationValue);
      
      // Send the final, clean data line. THIS IS ALL THE PYTHON SCRIPT WILL SEE.
      Serial.print(outputBuffer); 

      reset_state();
    }
  }
}