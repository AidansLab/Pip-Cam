/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Pip-Cam - image crop, brightness/contrast and dithering to the .img format
 * Copyright (C) 2026 Aidan Lee-Calamera
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

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

        std::vector<float> errRow(width + 2, 0.0f);
        std::vector<float> errNext(width + 2, 0.0f);

        for (int y = 0; y < height; y++) {
            errRow.swap(errNext);
            errNext.assign(width + 2, 0.0f);

            uint8_t currentByte = 0;
            int startBit = 8 - bpp;
            int bitShift = startBit;

            for (int x = 0; x < width; x++) {
                int idx = y * width + x;
                uint8_t pixel = rawData[idx];

                // Apply Brightness/Contrast
                float val = (float)pixel + brightness;
                val = (contrastFactor * (val - 128.0f)) + 128.0f;

                // Add Floyd-Steinberg error
                int ix = x + 1;
                val += errRow[ix];

                int clippedVal = max(0, min(255, (int)round(val)));

                // Quantize to 0-maxVal
                // e.g. 2-bit: 0-255 -> 0-3
                int quantized = (clippedVal * (maxVal + 1)) / 256;
                if (quantized > maxVal) quantized = maxVal;
                if (quantized < 0) quantized = 0;

                // Calculate error
                int outputVal = (quantized * 255) / maxVal;
                float error = val - outputVal;

                // Distribute error
                errRow[ix + 1] += error * (7.0f / 16.0f);
                errNext[ix - 1] += error * (3.0f / 16.0f);
                errNext[ix] += error * (5.0f / 16.0f);
                errNext[ix + 1] += error * (1.0f / 16.0f);

                // Pack bits
                currentByte |= (quantized << bitShift);
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
    // Effects are applied inline to support float-based error diffusion.
};

#endif // IMAGE_CONVERTER_H
