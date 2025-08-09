// Define the Serial port you'll use to connect to the STM32
#define STM32_SERIAL Serial2

// Use RX2 (GPIO 16) and TX2 (GPIO 17) for the STM32 connection
#define RXD2 16
#define TXD2 17
#define BAUD_RATE 115200

void setup() {
  Serial.begin(BAUD_RATE);
  Serial.println("ESP32 UART-to-USB Bridge Initialized.");
  
  STM32_SERIAL.begin(BAUD_RATE, SERIAL_8N1, RXD2, TXD2);
  Serial.println("Listening for data from STM32...");
}

void loop() {
  if (STM32_SERIAL.available()) {
    String dataFromSTM32 = STM32_SERIAL.readStringUntil('\n');
    Serial.println(dataFromSTM32);
  }
}