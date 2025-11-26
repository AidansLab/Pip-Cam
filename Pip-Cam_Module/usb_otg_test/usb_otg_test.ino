#include <Arduino.h>
#include <USBHostSerial.h>

// Initialize USBHostSerial
USBHostSerial usbHost;

// Target Device IDs
const uint16_t TARGET_VID = 0x0483;
const uint16_t TARGET_PID = 0xA4F1;

bool deviceConnected = false;
unsigned long lastCheckTime = 0;
const unsigned long CHECK_INTERVAL = 1000;

void setup() {
  // Serial is useful for the first few seconds before USB Host takes over
  Serial.begin(115200);
  
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW); // Start OFF

  // Wait a bit to allow upload to finish cleanly before switching modes
  delay(2000);

  Serial.println("Starting USB OTG Test...");
  
  // Initialize USB Host
  // 115200 baud, 1 stop bit (0), No parity (0), 8 data bits
  usbHost.begin(115200, 0, 0, 8);
}

void loop() {
  static unsigned long lastBlinkTime = 0;
  static bool ledState = false;
  static bool commandSent = false;
  static unsigned long commandSentTime = 0;

  // Check connection status
  if (usbHost) {
    if (!deviceConnected) {
      deviceConnected = true;
      digitalWrite(LED_BUILTIN, HIGH); // Solid ON when connected
      
      // Give it a moment to settle
      delay(1000);
      
      // Send the command
      const char* cmd = "g.clear();\n";
      usbHost.write((const uint8_t*)cmd, strlen(cmd));
      
      commandSent = true;
      commandSentTime = millis();
    }

    // Visual feedback for command sent: Fast blink for 2 seconds
    if (commandSent) {
      if (millis() - commandSentTime < 2000) {
        if (millis() - lastBlinkTime > 100) { // Fast blink 100ms
          lastBlinkTime = millis();
          ledState = !ledState;
          digitalWrite(LED_BUILTIN, ledState);
        }
      } else {
        // After 2 seconds, go back to Solid ON
        digitalWrite(LED_BUILTIN, HIGH); 
      }
    } else {
       digitalWrite(LED_BUILTIN, HIGH); // Ensure Solid ON if connected but not blinking
    }
    
    // Read any response (optional, just consumes buffer)
    while (usbHost.available()) {
      usbHost.read();
    }
    
  } else {
    // NOT CONNECTED
    if (deviceConnected) {
      deviceConnected = false;
      commandSent = false;
    }
    
    // Slow Blink (500ms) to indicate "Waiting..."
    if (millis() - lastBlinkTime > 500) {
      lastBlinkTime = millis();
      ledState = !ledState;
      digitalWrite(LED_BUILTIN, ledState);
    }
  }
}
