#include "esp_camera.h"
#include "FS.h"
#include "LittleFS.h"
#include "ImageConverter.h"
#include "img_converters.h" // For frame2jpg
#include <USBHostSerial.h>

// ===================
// Select camera model
// ===================
#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM
#include "camera_pins.h"

// ===========================
// USB Host Configuration
// ===========================
USBHostSerial usbHost;
const uint16_t TARGET_VID = 0x0483; // STM32
const uint16_t TARGET_PID = 0xA4F1; // Espruino

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
  STATE_UPLOAD_CHUNKS,
  STATE_UPLOAD_END,
  STATE_DONE
};

State currentState = STATE_INIT;
std::vector<uint8_t> imageData;
std::vector<uint8_t> lastCroppedImage; // Store last cropped image for reconvert
size_t uploadOffset = 0;
const size_t CHUNK_SIZE = 256; // Chunk size for USB transfer
unsigned long lastActivity = 0;
const unsigned long SLEEP_TIMEOUT = 60000; // 60 seconds timeout
int captureBrightness = CONVERT_BRIGHTNESS;
int captureContrast = CONVERT_CONTRAST;
String lastImageTimestamp = ""; // Last image timestamp (for reconvert)
String uploadImageTimestamp = ""; // Image timestamp being uploaded (accessible across states)
bool isReconvert = false; // Track if current operation is reconvert

// Power indicator pin
const int powerPin = D5;

void setup() {
  // 1. Initialize LittleFS
  if (!LittleFS.begin(true)) {
    return;
  }

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

  // 3. Initialize USB Host
  usbHost.begin(9600, 0, 0, 8); // Baud, Stop, Parity, Data

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  
  // Set D5 high to indicate power on
  pinMode(powerPin, OUTPUT);
  digitalWrite(powerPin, HIGH);

  currentState = STATE_WAIT_USB;
  lastActivity = millis();
}

void loop() {
  switch (currentState) {
    case STATE_WAIT_USB:
      // Check for USB connection timeout (30 seconds)
      if (millis() - lastActivity > 30000) {
        // Pull D5 low before deep sleep
        digitalWrite(powerPin, LOW);
        delay(100);
        esp_deep_sleep_start();
      }
      
      if (usbHost) { // Check if connected
        // We might want to check VID/PID if library supports it, 
        // but for now just assuming connection is enough or library handles it.
        // The simple USBHostSerial check just means "connected".
        delay(1000); // Give it a moment
        currentState = STATE_QUERY_DIR;
        lastActivity = millis(); // Reset activity timer
      } else {
        static unsigned long lastPrint = 0;
        if (millis() - lastPrint > 1000) {
          lastPrint = millis();
        }
      }
      break;

    case STATE_QUERY_DIR:
      {
        // Ensure IMGS directory exists by trying to create it
        String cmd = "\x10try{require('fs').mkdir('IMGS');}catch(e){}\n";
        usbHost.write((const uint8_t*)cmd.c_str(), cmd.length());
        delay(200);
        currentState = STATE_WAIT_COMMAND;
      }
      break;

    case STATE_WAIT_DIR_RESPONSE:
      // No longer used - kept for compatibility
      currentState = STATE_WAIT_COMMAND;
      break;

    case STATE_WAIT_COMMAND:
      // Check for timeout
      if (millis() - lastActivity > SLEEP_TIMEOUT) {
        // Pull D5 low before deep sleep
        digitalWrite(powerPin, LOW);
        delay(100);
        esp_deep_sleep_start();
      }

      if (usbHost.available()) {
        lastActivity = millis(); // Reset timer on activity
        String cmd = "";
        while (usbHost.available()) {
          char c = usbHost.read();
          cmd += c;
          delay(1); // Allow byte to arrive
        }
        
        if (cmd.indexOf("TakePic") >= 0) {
          
          // Parse brightness and contrast from command: "TakePic,brightness,contrast"
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
      }
      break;

    case STATE_CAPTURE:
      {
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
        
        // Save as JPEG
        uint8_t *jpg_buf = NULL;
        size_t jpg_len = 0;
        bool jpeg_converted = frame2jpg(fb, 80, &jpg_buf, &jpg_len);
        
        if (jpeg_converted) {
            File file = LittleFS.open("/capture.jpg", FILE_WRITE);
            if (file) {
                file.write(jpg_buf, jpg_len);
                file.close();
            }
            free(jpg_buf);
        } else {
        }

        // Convert immediately
        // First crop to 400x208
        int targetW = 400;
        int targetH = 208;
        std::vector<uint8_t> cropped = ImageConverter::crop(fb->buf, fb->width, fb->height, targetW, targetH);
        
        // Store cropped image for potential reconvert
        lastCroppedImage = cropped;
        
        imageData = ImageConverter::convert(cropped.data(), targetW, targetH, captureBrightness, captureContrast, CONVERT_BPP);

        // Save converted image for debug
        File convertedFile = LittleFS.open("/converted.img", FILE_WRITE);
        if (convertedFile) {
            convertedFile.write(imageData.data(), imageData.size());
            convertedFile.close();
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
        // Ensure IMGS directory exists (already done in STATE_QUERY_DIR, but just in case)
        String cmd = "\x10try{require('fs').mkdir(\"IMGS\");}catch(e){}\n";
        usbHost.write((const uint8_t*)cmd.c_str(), cmd.length());
        delay(100);

        // Open file on Pip-Boy with timestamp-based filename
        // For reconvert: uploadImageTimestamp is already set to lastImageTimestamp
        // For new capture: uploadImageTimestamp was set in STATE_WAIT_TIMESTAMP_RESPONSE
        cmd = "\x10var p=E.openFile(\"IMGS/" + uploadImageTimestamp + ".img\",\"w\");\n";
        usbHost.write((const uint8_t*)cmd.c_str(), cmd.length());
        delay(100); // Wait for processing
        
        uploadOffset = 0;
        currentState = STATE_UPLOAD_CHUNKS;
      }
      break;

    case STATE_UPLOAD_CHUNKS:
      if (uploadOffset < imageData.size()) {
        size_t remaining = imageData.size() - uploadOffset;
        size_t thisChunkSize = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;

        // Construct the write command
        // f.write(String.fromCharCode.apply(null, [bytes...]));
        //String cmd = "\x10p.write(String.fromCharCode.apply(null,[";
        String cmd = "\x10p.write([";
        
        // Append bytes
        for (size_t i = 0; i < thisChunkSize; i++) {
          cmd += String(imageData[uploadOffset + i]);
          if (i < thisChunkSize - 1) {
            cmd += ",";
          }
        }
        cmd += "]);\n";
        //cmd += "]));\n";

        // Send command
        usbHost.write((const uint8_t*)cmd.c_str(), cmd.length());
        
        uploadOffset += thisChunkSize;

        // Flash LED
        static unsigned long lastToggle = 0;
        if (millis() - lastToggle > 100) {
          static bool ledState = false;
          ledState = !ledState;
          digitalWrite(LED_BUILTIN, ledState);
          lastToggle = millis();
        }
        
        delay(20); // Small delay between chunks to prevent buffer overflow on receiver
      } else {
        currentState = STATE_UPLOAD_END;
      }
      break;

    case STATE_UPLOAD_END:
      {
        // Close file
        String cmd = "\x10p.close();\n";
        usbHost.write((const uint8_t*)cmd.c_str(), cmd.length());
        delay(100);

        cmd = "\x10p = undefined;\n";
        usbHost.write((const uint8_t*)cmd.c_str(), cmd.length());

        cmd = "\x10process.memory(true);\n";
        usbHost.write((const uint8_t*)cmd.c_str(), cmd.length());
        delay(100);

        // Send update flag to refresh the image display
        cmd = "\x10Pip.emit('updateImg');\n";
        usbHost.write((const uint8_t*)cmd.c_str(), cmd.length());

        digitalWrite(LED_BUILTIN, LOW); // Turn off LED
        currentState = STATE_DONE;
      }
      break;

    case STATE_DONE:
      // Loop back to wait for next command
      currentState = STATE_WAIT_COMMAND;
      break;
  }
}
