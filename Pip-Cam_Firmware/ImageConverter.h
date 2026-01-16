#ifndef IMAGE_CONVERTER_H
#define IMAGE_CONVERTER_H

#include <Arduino.h>
#include <vector>
#include <cmath>
#include <mbedtls/base64.h>

class ImageConverter {
public:
    static std::vector<uint8_t> convert(uint8_t* rawData, int width, int height, int brightness, int contrast, int bpp) {
        // Validate BPP
        if (bpp != 1 && bpp != 2 && bpp != 4 && bpp != 8) {
            bpp = 2; // Default
        }

        // 1. Prepare Output Buffer
        // Header: 8 bytes [W_lo, W_hi, H_lo, H_hi, BPP, 0, 0, 0]
        // Data: Rows padded to nearest byte
        int rowBits = width * bpp;
        int rowBytes = (rowBits + 7) / 8;
        int totalSize = 8 + (rowBytes * height);
        
        std::vector<uint8_t> buffer(totalSize);

        // 2. Write Header
        buffer[0] = width & 0xFF;
        buffer[1] = (width >> 8) & 0xFF;
        buffer[2] = height & 0xFF;
        buffer[3] = (height >> 8) & 0xFF;
        buffer[4] = bpp;
        buffer[5] = 0;
        buffer[6] = 0;
        buffer[7] = 0;

        // 3. Process Pixels
        int offset = 8;
        float contrastFactor = (259.0f * (contrast + 255.0f)) / (255.0f * (259.0f - contrast));
        int maxVal = (1 << bpp) - 1;

        for (int y = 0; y < height; y++) {
            uint8_t currentByte = 0;
            int startBit = 8 - bpp;
            int bitShift = startBit;

            for (int x = 0; x < width; x++) {
                int idx = y * width + x;
                uint8_t pixel = rawData[idx];

                // Apply Brightness/Contrast
                int val = applyEffects(pixel, brightness, contrastFactor);

                // Quantize to 0-maxVal
                // e.g. 2-bit: 0-255 -> 0-3
                val = (val * (maxVal + 1)) / 256;
                if (val > maxVal) val = maxVal;
                if (val < 0) val = 0;

                // Pack bits
                currentByte |= (val << bitShift);
                bitShift -= bpp;

                // Byte full? Write it.
                if (bitShift < 0) {
                    buffer[offset++] = currentByte;
                    currentByte = 0;
                    bitShift = startBit;
                }
            }

            // End of Row: Write partial byte if exists (Row Alignment)
            if (bitShift != startBit) {
                buffer[offset++] = currentByte;
            }
        }

        return buffer;
    }

    static std::vector<uint8_t> crop(uint8_t* rawData, int srcWidth, int srcHeight, int targetWidth, int targetHeight) {
        if (targetWidth > srcWidth || targetHeight > srcHeight) {
            // Error or just return original? For now return empty or original.
            // Let's return original if invalid, but better to handle correctly.
            // For simplicity, if invalid, return a copy of rawData (assuming it matches srcWidth/Height)
            // But rawData is a pointer.
            // Let's just clamp.
            targetWidth = min(targetWidth, srcWidth);
            targetHeight = min(targetHeight, srcHeight);
        }

        std::vector<uint8_t> cropped(targetWidth * targetHeight);
        
        int startX = (srcWidth - targetWidth) / 2;
        int startY = (srcHeight - targetHeight) / 2;

        for (int y = 0; y < targetHeight; y++) {
            for (int x = 0; x < targetWidth; x++) {
                int srcIdx = (startY + y) * srcWidth + (startX + x);
                int destIdx = y * targetWidth + x;
                cropped[destIdx] = rawData[srcIdx];
            }
        }
        return cropped;
    }

private:
    static int applyEffects(int val, int brightness, float contrastFactor) {
        val += brightness;
        val = (int)((contrastFactor * (val - 128)) + 128);
        return max(0, min(255, val));
    }
};

#endif // IMAGE_CONVERTER_H
