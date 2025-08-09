#define STM32_SERIAL Serial2
#define RXD2 16
#define TXD2 17
#define BAUD_RATE 115200

// --- State Machine Variables ---
// These variables will store the parsed data as it arrives.
float g_temperature = 0.0;
float g_waterLevelPercent = 0.0;
int   g_anomalyStatus = 0; // 0=NOMINAL, 1=ANOMALY
int   g_pumpStatus = 0;    // 0=OFF, 1=ON
float g_vibrationValue = 0.0; // The 'similarity' value from the STM32

// These flags track if we've received each piece of data for the current batch.
bool g_receivedTemp = false;
bool g_receivedWater = false;
bool g_receivedAnomaly = false;
bool g_receivedPump = false;


void setup() {
  // Start the primary USB serial connection to the PC
  Serial.begin(BAUD_RATE);
  // Start the secondary serial connection to the STM32
  STM32_SERIAL.begin(BAUD_RATE, SERIAL_8N1, RXD2, TXD2);

  Serial.println("ESP32 Intelligent Parser Initialized. Waiting for data...");
}

void loop() {
  // Check if any data is available from the STM32
  if (STM32_SERIAL.available()) {
    // Read one full line of text, ending with a newline character
    String line = STM32_SERIAL.readStringUntil('\n');
    line.trim(); // IMPORTANT: Remove any \r characters

    // --- PARSING LOGIC ---
    // Check what kind of data this line contains and parse it.

    if (line.startsWith("NOMINAL") || line.startsWith("ANOMALY")) {
      g_anomalyStatus = line.startsWith("ANOMALY") ? 1 : 0;
      
      int commaIndex = line.indexOf(',');
      if (commaIndex != -1) {
        // The vibration value is the 'similarity' score after the comma
        g_vibrationValue = line.substring(commaIndex + 1).toFloat();
      }
      g_receivedAnomaly = true;

    } else if (line.startsWith("WaterLevel")) {
      int firstComma = line.indexOf(',');
      int secondComma = line.indexOf(',', firstComma + 1);
      if (secondComma != -1) {
        // The percentage is the value between the second comma and the '%' sign
        String percentStr = line.substring(secondComma + 1);
        g_waterLevelPercent = percentStr.toFloat();
      }
      g_receivedWater = true;
      
    } else if (line.startsWith("Pump")) {
      g_pumpStatus = (line.indexOf("ON") != -1) ? 1 : 0;
      g_receivedPump = true;
      
    } else if (line.startsWith("WaterTemp")) {
      int commaIndex = line.indexOf(',');
      if (commaIndex != -1) {
        g_temperature = line.substring(commaIndex + 1).toFloat();
      }
      g_receivedTemp = true;
    }

    // --- ASSEMBLY AND TRANSMISSION LOGIC ---
    // Check if we have now received a complete batch of data.
    if (g_receivedTemp && g_receivedWater && g_receivedAnomaly && g_receivedPump) {
      
      // Create a buffer to hold the final, formatted string
      char outputBuffer[128];

      // Assemble the final string in the EXACT order the Python script expects.
      // Format: temp,level,anomaly,pump,vibration
      sprintf(outputBuffer, "%.2f,%.2f,%d,%d,%.2f\n",
              g_temperature,
              g_waterLevelPercent,
              g_anomalyStatus,
              g_pumpStatus,
              g_vibrationValue);
      
      // Send the completed string to the PC over USB.
      Serial.print(outputBuffer);

      // CRUCIAL: Reset all flags to prepare for the next batch of data.
      g_receivedTemp = false;
      g_receivedWater = false;
      g_receivedAnomaly = false;
      g_receivedPump = false;
    }
  }
}