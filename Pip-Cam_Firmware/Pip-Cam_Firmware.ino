#include "esp_camera.h"
#include "ImageConverter.h"
#include <USBHostSerial.h>
#include "driver/rtc_io.h"
#include "hal/usb_serial_jtag_ll.h"
#include "soc/usb_serial_jtag_reg.h"

// ===================
// Select camera model
// ===================
#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM
#include "camera_pins.h"

// ===========================
// USB Host Configuration
// ===========================
USBHostSerial usbHost;
//const uint16_t TARGET_VID = 0x0483; // STM32
//const uint16_t TARGET_PID = 0xA4F1; // Espruino

// ===========================
// Image Conversion Settings
// ===========================
#define CONVERT_BRIGHTNESS 0
#define CONVERT_CONTRAST 0
#define CONVERT_BPP 4

// ===========================
// State Machine
// ===========================
enum State {
  STATE_INIT,
  STATE_WAIT_USB,
  STATE_QUERY_DIR,
  STATE_WAIT_DIR_RESPONSE,
  STATE_WAIT_COMMAND,
  STATE_CAPTURE,
  STATE_QUERY_TIMESTAMP,
  STATE_WAIT_TIMESTAMP_RESPONSE,
  STATE_RECONVERT,
  STATE_CONVERT,
  STATE_UPLOAD_START,
  STATE_WAIT_FILE_SEND_ACK,
  STATE_UPLOAD_CHUNKS,
  STATE_WAIT_DATA_ACK,
  STATE_UPLOAD_END,
  STATE_DONE
};

// Packet protocol constants
const uint8_t DLE = 0x10;  // Data Link Escape
const uint8_t SOH = 0x01;  // Start of Heading
const uint8_t ACK = 0x06;  // Acknowledgment
const uint8_t NAK = 0x15;  // Negative Acknowledgment
const uint16_t PKT_FILE_SEND = 0x6000;
const uint16_t PKT_DATA = 0x8000;

State currentState = STATE_INIT;
std::vector<uint8_t> imageData;
std::vector<uint8_t> lastCroppedImage; // Store last cropped image for reconvert
size_t uploadOffset = 0;
const size_t CHUNK_SIZE = 256; // Chunk size for USB transfer
unsigned long lastActivity = 0;
const unsigned long SLEEP_TIMEOUT = 60000; // 60 seconds timeout
int captureBrightness = CONVERT_BRIGHTNESS;
int captureContrast = CONVERT_CONTRAST;
bool captureCorrupt = false; // Whether to apply artificial corruption
String lastImageTimestamp = ""; // Last image timestamp (for reconvert)
String uploadImageTimestamp = ""; // Image timestamp being uploaded (accessible across states)
bool isReconvert = false; // Track if current operation is reconvert
bool lastButtonState = false; // Track button state for edge detection
unsigned long lastButtonTime = 0; // Debounce timer
unsigned long packetSentTime = 0; // Timeout for ACK waiting
const unsigned long ACK_TIMEOUT = 2000; // 2 second timeout for ACK

// Power indicator pin
const int powerPin = D4;
#define WAKEUP_GPIO GPIO_NUM_4

void setup() {
  


  // 2. Initialize Camera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  
  // Use GRAYSCALE for easier processing
  config.pixel_format = PIXFORMAT_GRAYSCALE; 
  config.frame_size = FRAMESIZE_HVGA; // 480x320, will crop to 400x208
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 10;
  config.fb_count = 1;

  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    return;
  }

  // Flip camera vertically (upside down)
  sensor_t *s = esp_camera_sensor_get();
  s->set_vflip(s, 1);
  
  // Power down camera to save power when idle
  // Register 0x3008: Bit 6 is Software Power Down
  if (s) {
    s->set_reg(s, 0x3008, 0x40, 0x40);
  }

  // 3. Initialize USB Host
  usbHost.begin(9600, 0, 0, 8); // Baud, Stop, Parity, Data

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO, 1); 

  rtc_gpio_pullup_dis(WAKEUP_GPIO);
  rtc_gpio_pulldown_en(WAKEUP_GPIO);
    
  // Set WAKEUP_GPIO as input with pulldown for button reading
  pinMode(WAKEUP_GPIO, INPUT_PULLDOWN);
  
  // Set D5 high to power on USB
  pinMode(powerPin, OUTPUT);
  digitalWrite(powerPin, HIGH);

  currentState = STATE_WAIT_USB;
  lastActivity = millis();
  
  // Log boot marker

}

// Send a packet using the Espruino protocol
// type: PKT_FILE_SEND or PKT_DATA
// data: the payload string
void sendPacket(uint16_t type, const String& data) {
  uint16_t flags = type | (data.length() & 0x1FFF);
  
  // Send header: DLE, SOH, flags high byte, flags low byte
  usbHost.write(DLE);
  usbHost.write(SOH);
  usbHost.write((flags >> 8) & 0xFF);
  usbHost.write(flags & 0xFF);
  
  // Send data
  for (size_t i = 0; i < data.length(); i++) {
    usbHost.write((uint8_t)data[i]);
  }
}

// Check for ACK/NAK response, returns 1 for ACK, -1 for NAK, 0 for nothing
int checkAck() {
  while (usbHost.available()) {
    uint8_t c = usbHost.read();
    if (c == ACK) return 1;
    if (c == NAK) return -1;
  }
  return 0;
}



void goToDeepSleep() {
  // 1. Put OV3660 into software standby
  sensor_t * s = esp_camera_sensor_get();
  if (s) {
    // Register 0x3008: Bit 6 is Software Power Down
    // Mask 0x40 (01000000)
    s->set_reg(s, 0x3008, 0x40, 0x40); 
  }
  delay(10); // Give the sensor a moment to power down

  // 2. Turn off the USB power
  digitalWrite(powerPin, LOW);

  delay(100);

  // 3. Optional: De-initialize camera to release GPIOs
  esp_camera_deinit();
  //CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);

  // 4. Final entry into deep sleep
  esp_deep_sleep_start();
}

void loop() {
  switch (currentState) {
    case STATE_WAIT_USB:
      // Check for USB connection timeout (10 seconds)
      if (millis() - lastActivity > 10000) {
        goToDeepSleep();
      }
      
      if (usbHost) { // Check if connected
        //delay(200); // Give it a moment
        String cmd = "\x10try{Pip.emit('drawFooter');}catch(e){}\n";
        usbHost.write((const uint8_t*)cmd.c_str(), cmd.length());
        currentState = STATE_WAIT_COMMAND;
        lastActivity = millis(); // Reset activity timer
      } else {
        // Blink LED while searching for USB connection
        static unsigned long lastBlink = 0;
        if (millis() - lastBlink > 500) {
          static bool ledState = false;
          ledState = !ledState;
          digitalWrite(LED_BUILTIN, ledState);
          lastBlink = millis();
        }
      }
      break;

    //case STATE_QUERY_DIR:
    //  {
    //    // Ensure IMGS directory exists by trying to create it
    //    String cmd = "\x10try{require('fs').mkdir('IMGS');}catch(e){}\n";
    //    usbHost.write((const uint8_t*)cmd.c_str(), cmd.length());
    //    delay(200);
    //    currentState = STATE_WAIT_COMMAND;
    //  }
    //  break;
//
    //case STATE_WAIT_DIR_RESPONSE:
    //  // No longer used - kept for compatibility
    //  currentState = STATE_WAIT_COMMAND;
    //  break;

    case STATE_WAIT_COMMAND:
      // Check for timeout
      if (millis() - lastActivity > SLEEP_TIMEOUT) {
        goToDeepSleep();
      }

      if (usbHost.available()) {
        lastActivity = millis(); // Reset timer on activity
        String cmd = "";
        unsigned long readStart = millis();
        // Read until newline or timeout (100ms)
        while (millis() - readStart < 100) {
          while (usbHost.available()) {
            char c = usbHost.read();
            cmd += c;
            if (c == '\n') break; // Complete command received
          }
          if (cmd.indexOf('\n') >= 0) break; // Got newline, stop waiting
          delay(5); // Wait for more data
        }
        
        if (cmd.indexOf("TakePic") >= 0) {
          
          // Parse brightness, contrast, corrupt from command: "TakePic,brightness,contrast,corrupt"
          int firstComma = cmd.indexOf(',');
          int secondComma = cmd.indexOf(',', firstComma + 1);
          int thirdComma = cmd.indexOf(',', secondComma + 1);
          
          if (firstComma > 0 && secondComma > firstComma) {
            String brightnessStr = cmd.substring(firstComma + 1, secondComma);
            String contrastStr;
            if (thirdComma > secondComma) {
              contrastStr = cmd.substring(secondComma + 1, thirdComma);
              String corruptStr = cmd.substring(thirdComma + 1);
              corruptStr.trim();
              captureCorrupt = (corruptStr == "true" || corruptStr == "1");
            } else {
              contrastStr = cmd.substring(secondComma + 1);
              captureCorrupt = false;
            }
            
            captureBrightness = brightnessStr.toInt();
            captureContrast = contrastStr.toInt();
          } else {
            // Use defaults if parsing fails
            captureBrightness = CONVERT_BRIGHTNESS;
            captureContrast = CONVERT_CONTRAST;
            captureCorrupt = false;
          }
          
          isReconvert = false; // This is a new capture
          currentState = STATE_CAPTURE;
        }
        else if (cmd.indexOf("Reconvert") >= 0) {
          
          // Parse brightness and contrast from command: "Reconvert,brightness,contrast"
          int firstComma = cmd.indexOf(',');
          int secondComma = cmd.indexOf(',', firstComma + 1);
          
          if (firstComma > 0 && secondComma > firstComma) {
            String brightnessStr = cmd.substring(firstComma + 1, secondComma);
            String contrastStr = cmd.substring(secondComma + 1);
            
            captureBrightness = brightnessStr.toInt();
            captureContrast = contrastStr.toInt();
          } else {
            // Use defaults if parsing fails
            captureBrightness = CONVERT_BRIGHTNESS;
            captureContrast = CONVERT_CONTRAST;
          }
          
          isReconvert = true; // This is a reconvert operation
          currentState = STATE_RECONVERT;
        }
        else if (cmd.indexOf("Gary!") >= 0) {
          // Handshake from Pip-Boy - respond to confirm connection
          String response = "\x10" "camConnected = true;\n";
          usbHost.write((const uint8_t*)response.c_str(), response.length());
          delay(100);
          String cmd = "\x10try{Pip.emit('drawFooter');}catch(e){}\n";
          usbHost.write((const uint8_t*)cmd.c_str(), cmd.length());
        }
      }
      
      // Always update button state for edge detection, even when processing commands
      {
        bool currentButtonState = digitalRead(WAKEUP_GPIO);
        if (currentButtonState && !lastButtonState && (millis() - lastButtonTime > 200)) {
          // Button pressed (rising edge with debounce)
          if (usbHost && currentState == STATE_WAIT_COMMAND) {
            String cmd = "\x10Pip.emit('takeFromCam');\n";
            usbHost.write((const uint8_t*)cmd.c_str(), cmd.length());
          }
          lastButtonTime = millis();
        }
        lastButtonState = currentButtonState;
      }
      break;

    case STATE_CAPTURE:
      {
        // Power up camera before capture
        sensor_t *s = esp_camera_sensor_get();
        if (s) {
          s->set_reg(s, 0x3008, 0x40, 0x00); // Clear bit 6 to wake up
        }
        delay(300); // Give camera time to wake up and stabilize
        
        // Flush the buffer to get a fresh frame
        // The camera driver often buffers the *next* frame immediately after the previous one is returned.
        // So we grab that stale frame and discard it to force a new capture.
        camera_fb_t * fb = esp_camera_fb_get();
        if (fb) {
            esp_camera_fb_return(fb);
        }
        
        // Now grab the fresh frame
        fb = esp_camera_fb_get();
        if (!fb) {
          delay(1000);
          return; // Retry?
        }
        


        // Convert immediately
        // First crop to 400x208
        int targetW = 400;
        int targetH = 208;
        std::vector<uint8_t> cropped = ImageConverter::crop(fb->buf, fb->width, fb->height, targetW, targetH);
        
        // Store cropped image for potential reconvert
        lastCroppedImage = cropped;
        
        imageData = ImageConverter::convert(cropped.data(), targetW, targetH, captureBrightness, captureContrast, CONVERT_BPP);

        // Apply artificial corruption if requested (only ~20% of the time when enabled)
        if (captureCorrupt && imageData.size() > 200 && random(100) < 20) {
          // Define safe margins from start and end (in bytes)
          const size_t marginStart = 18000;  // Don't corrupt first 18000 bytes
          const size_t marginEnd = 10000;    // Don't corrupt last 10000 bytes
          const size_t minInsert = 20;       // Minimum random chars to insert
          const size_t maxInsert = 50;      // Maximum random chars to insert
          
          // Calculate safe range
          size_t safeStart = marginStart;
          size_t safeEnd = imageData.size() - marginEnd;
          
          if (safeEnd > safeStart) {
            // Random number of characters to insert
            size_t insertCount = minInsert + (random(maxInsert - minInsert + 1));
            
            // Insert each random byte at its own random position
            for (size_t i = 0; i < insertCount; i++) {
              // Recalculate safe end since we're inserting (it grows each time)
              size_t currentSafeEnd = imageData.size() - marginEnd;
              size_t insertPos = safeStart + random(currentSafeEnd - safeStart);
              uint8_t randomByte = (uint8_t)random(256);
              imageData.insert(imageData.begin() + insertPos, randomByte);
            }
          }
        }



        esp_camera_fb_return(fb);
        
        // For new captures, query timestamp from Pip-Boy
        // For reconvert, go directly to upload (reuse lastImageTimestamp)
        if (!isReconvert) {
          currentState = STATE_QUERY_TIMESTAMP;
        } else {
          currentState = STATE_UPLOAD_START;
        }
      }
      break;

    case STATE_QUERY_TIMESTAMP:
      {
        // Query the timestamp from Pip-Boy
        // Date().toLocalISOString() returns "2025-12-13T16:49:09.251-0500"
        // We chop off the last 9 characters and replace colons with dashes for valid filename
        // Result: "2025-12-13T16-49-09"
        String cmd = "\x10print(Date().toLocalISOString().slice(0,-9).replace(/:/g,'-'));\n";
        usbHost.write((const uint8_t*)cmd.c_str(), cmd.length());
        currentState = STATE_WAIT_TIMESTAMP_RESPONSE;
      }
      break;

    case STATE_WAIT_TIMESTAMP_RESPONSE:
      {
        static unsigned long queryStartTime = 0;
        if (queryStartTime == 0) {
          queryStartTime = millis();
        }
        
        if (usbHost.available()) {
          String response = "";
          unsigned long startTime = millis();
          // Read for up to 2 seconds
          while (millis() - startTime < 2000) {
            while (usbHost.available()) {
              char c = usbHost.read();
              response += c;
            }
            // Check if we have a timestamp (contains T and -)
            if (response.indexOf('T') >= 0 && response.indexOf('-') >= 0 && response.length() >= 19) {
              break;
            }
            delay(10);
          }
          
          // Parse the timestamp - find the ISO string format
          // Response format: "1999-12-31T19:02:26\n=undefined" or similar
          // Look for the 'T' separator and work backwards/forwards from there
          int tIdx = response.indexOf('T');
          if (tIdx >= 10 && tIdx + 9 <= response.length()) {
            // Extract 10 chars before T (YYYY-MM-DD) + T + 8 chars after (HH:MM:SS) = 19 chars total
            int startIdx = tIdx - 10;
            uploadImageTimestamp = response.substring(startIdx, startIdx + 19);
            lastImageTimestamp = uploadImageTimestamp; // Remember for reconvert
          } else {
            // Fallback: use a default timestamp
            uploadImageTimestamp = "unknown";
            lastImageTimestamp = uploadImageTimestamp;
          }
          
          queryStartTime = 0;
          currentState = STATE_UPLOAD_START;
        } else {
          // Check for timeout
          if (millis() - queryStartTime > 3000) {
            // Timeout, use fallback
            uploadImageTimestamp = "unknown";
            lastImageTimestamp = uploadImageTimestamp;
            queryStartTime = 0;
            currentState = STATE_UPLOAD_START;
          }
        }
      }
      break;

    case STATE_RECONVERT:
      {
        // Check if we have a previous image to reconvert
        if (lastCroppedImage.size() == 0 || lastImageTimestamp.length() == 0) {
          // No previous image or timestamp, go back to waiting
          currentState = STATE_WAIT_COMMAND;
          break;
        }
        
        // Reconvert the last cropped image with new brightness/contrast
        int targetW = 400;
        int targetH = 208;
        imageData = ImageConverter::convert(lastCroppedImage.data(), targetW, targetH, captureBrightness, captureContrast, CONVERT_BPP);

        // Use the same timestamp as the last image for reconvert
        uploadImageTimestamp = lastImageTimestamp;

        currentState = STATE_UPLOAD_START;
      }
      break;

    case STATE_UPLOAD_START:
      {        
        // Build filename for the image
        String filename = "IMGS/" + uploadImageTimestamp + ".img";
        
        // Build FILE_SEND packet payload with fs:1 to use filesystem (not internal storage)
        String payload = "{fn:\"" + filename + "\",s:" + String(imageData.size()) + ",fs:1}";
        

        
        // Send FILE_SEND packet
        sendPacket(PKT_FILE_SEND, payload);
        
        uploadOffset = 0;
        packetSentTime = millis();
        currentState = STATE_WAIT_FILE_SEND_ACK;
      }
      break;

    case STATE_WAIT_FILE_SEND_ACK:
      {
        int ack = checkAck();
        if (ack == 1) {
          // ACK received, start sending data chunks

          currentState = STATE_UPLOAD_CHUNKS;
        } else if (ack == -1) {
          // NAK received, retry or fail

          currentState = STATE_DONE; // Give up for now
        } else if (millis() - packetSentTime > ACK_TIMEOUT) {
          // Timeout, give up

          currentState = STATE_DONE;
        }
      }
      break;

    case STATE_UPLOAD_CHUNKS:
      {
        if (uploadOffset < imageData.size()) {
          // Calculate chunk size
          const size_t PACKET_CHUNK = 1024;
          size_t remaining = imageData.size() - uploadOffset;
          size_t thisChunkSize = (remaining > PACKET_CHUNK) ? PACKET_CHUNK : remaining;

          // Build DATA packet payload as raw bytes
          String payload = "";
          for (size_t i = 0; i < thisChunkSize; i++) {
            payload += (char)imageData[uploadOffset + i];
          }
          
          // Send DATA packet
          sendPacket(PKT_DATA, payload);
          
          uploadOffset += thisChunkSize;
          packetSentTime = millis();
          currentState = STATE_WAIT_DATA_ACK;

          // Flash LED
          static unsigned long lastToggle = 0;
          if (millis() - lastToggle > 100) {
            static bool ledState = false;
            ledState = !ledState;
            digitalWrite(LED_BUILTIN, ledState);
            lastToggle = millis();
          }
        } else {
          currentState = STATE_UPLOAD_END;
        }
      }
      break;

    case STATE_WAIT_DATA_ACK:
      {
        int ack = checkAck();
        if (ack == 1) {
          // ACK received, send next chunk or finish
          if (uploadOffset < imageData.size()) {
            currentState = STATE_UPLOAD_CHUNKS;
          } else {
            currentState = STATE_UPLOAD_END;
          }
        } else if (ack == -1) {
          // NAK received, give up

          currentState = STATE_DONE;
        } else if (millis() - packetSentTime > ACK_TIMEOUT) {
          // Timeout, give up

          currentState = STATE_DONE;
        }
      }
      break;

    case STATE_UPLOAD_END:
      {

        
        // Packet protocol handles file completion automatically
        // Just run garbage collection and send update event
        delay(100); // Brief delay to let protocol finish
        
        // Force garbage collection
        String cmd = "\x10process.memory(true);\n";
        usbHost.write((const uint8_t*)cmd.c_str(), cmd.length());
        delay(100);
        
        // Touch filesystem to refresh state (helps prevent NO_PATH errors)
        cmd = "\x10try{require('fs').statSync('IMGS');}catch(e){}\n";
        usbHost.write((const uint8_t*)cmd.c_str(), cmd.length());
        delay(50);

        // Send update flag to refresh the image display
        cmd = "\x10Pip.emit('updateImg');\n";
        usbHost.write((const uint8_t*)cmd.c_str(), cmd.length());

        digitalWrite(LED_BUILTIN, LOW); // Turn off LED
        currentState = STATE_DONE;
      }
      break;

    case STATE_DONE:
      {
        // Power down camera to save power when idle
        sensor_t *s = esp_camera_sensor_get();
        if (s) {
          s->set_reg(s, 0x3008, 0x40, 0x40); // Set bit 6 for power down
        }
        
        // Loop back to wait for next command
        currentState = STATE_WAIT_COMMAND;
      }
      break;
  }
}
