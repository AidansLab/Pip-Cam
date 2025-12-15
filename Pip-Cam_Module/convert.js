const fs = require('fs');
const { createCanvas, loadImage } = require('canvas');

const INPUT_FILE = process.argv[2];
const OUTPUT_FILE = process.argv[3] || 'image.img';
const BRIGHTNESS = parseInt(process.argv[4]) || 0;
const CONTRAST = parseInt(process.argv[5]) || 0;
let BPP = parseInt(process.argv[6]) || 2; // Default to 2-bit

// Validate BPP
if (![1, 2, 4, 8].includes(BPP)) {
    console.log("Invalid BPP. Defaulting to 2. Supported: 1, 2, 4, 8");
    BPP = 2;
}

if (!INPUT_FILE) {
    console.log("Usage: node convert.js <input> [output] [brightness] [contrast] [bpp]");
    process.exit(1);
}

async function convert() {
    try {
        const image = await loadImage(INPUT_FILE);
        console.log(`Processing ${image.width}x${image.height} image at ${BPP} BPP...`);

        // 1. Get Raw Pixel Data
        const canvas = createCanvas(image.width, image.height);
        const ctx = canvas.getContext('2d');
        ctx.drawImage(image, 0, 0);
        const data = ctx.getImageData(0, 0, image.width, image.height).data;

        // 2. Prepare Output Buffer
        // Header: 8 bytes [W_lo, W_hi, H_lo, H_hi, BPP, 0, 0, 0]
        // Data: Rows padded to nearest byte
        const rowBytes = Math.ceil((image.width * BPP) / 8);
        const totalSize = 8 + (rowBytes * image.height);
        const buffer = Buffer.alloc(totalSize);

        // 3. Write Header
        buffer.writeUInt16LE(image.width, 0);
        buffer.writeUInt16LE(image.height, 2);
        buffer[4] = BPP; 

        // 4. Process Pixels
        let offset = 8; // Start after header
        const contrastFactor = (259 * (CONTRAST + 255)) / (255 * (259 - CONTRAST));
        const maxVal = (1 << BPP) - 1; // Max value for this BPP (e.g., 3 for 2bpp, 15 for 4bpp)

        for (let y = 0; y < image.height; y++) {
            let currentByte = 0;
            let startBit = 8 - BPP; // MSB first (Espruino format)
            let bitShift = startBit;
            
            for (let x = 0; x < image.width; x++) {
                const idx = (y * image.width + x) * 4;
                let r = data[idx];
                let g = data[idx+1];
                let b = data[idx+2];

                // Apply Brightness/Contrast
                r = applyEffects(r, BRIGHTNESS, contrastFactor);
                g = applyEffects(g, BRIGHTNESS, contrastFactor);
                b = applyEffects(b, BRIGHTNESS, contrastFactor);

                // Grayscale
                let gray = (r + g + b) / 3;
                
                // Quantize to 0-maxVal
                // e.g. 2-bit: 0-255 -> 0-3
                let val = Math.floor((gray * (maxVal + 1)) / 256);
                if (val > maxVal) val = maxVal;
                if (val < 0) val = 0;

                // Pack bits
                currentByte |= (val << bitShift);
                bitShift -= BPP;

                // Byte full? Write it.
                if (bitShift < 0) {
                    buffer[offset++] = currentByte;
                    currentByte = 0;
                    bitShift = startBit;
                }
            }

            // End of Row: Write partial byte if exists (Row Alignment)
            if (bitShift !== startBit) {
                buffer[offset++] = currentByte;
            }
        }

        fs.writeFileSync(OUTPUT_FILE, buffer);
        console.log(`Success! Saved '${OUTPUT_FILE}' (${totalSize} bytes).`);

    } catch (err) {
        console.error("Error:", err.message);
    }
}

function applyEffects(val, brightness, contrastFactor) {
    val += brightness;
    val = (contrastFactor * (val - 128)) + 128;
    return Math.max(0, Math.min(255, val));
}

convert();