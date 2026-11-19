# $\Huge{\textbf{Pip-Cam}}$
Your companion for documenting the wasteland ~~and your untimely demise~~

The Pip-Cam is a small camera module that plugs into the USB-C port of a Pip-Boy (MK V or Pip-Boy 3000). It takes a grayscale photo, dithers it down to the Pip-Boy's screen format, and uploads it straight to the Pip-Boy's filesystem. You can then browse and delete photos from a Pip-Cam app on the Pip-Boy, and re-tune the brightness and contrast of the latest photo.

| Pip-Cam MK V | Pip-Cam 3000 |
| --- | --- |
| See page 1 of [Pip-Cam_Manuals.pdf](Pip-Cam_Manuals.pdf) | See page 2 of [Pip-Cam_Manuals.pdf](Pip-Cam_Manuals.pdf) |

---

## Contents

- [How it works](#how-it-works)
- [Repository layout](#repository-layout)
- [Bill of materials](#bill-of-materials)
- [Ordering the PCBs](#ordering-the-pcbs)
- [Assembly](#assembly)
- [Programming the firmware](#programming-the-firmware)
- [Installing the Pip-Boy app](#installing-the-pip-boy-app)
- [Using the Pip-Cam](#using-the-pip-cam)
- [Troubleshooting](#troubleshooting)
- [License](#license)

---

## How it works

```
 ┌──────────────┐  BAT   ┌──────────────────────────┐  D+/D-  ┌──────────────┐
 │ 3.7V LiPo    ├───────►│ XIAO ESP32-S3 Sense      ├────────►│ USB-C plug   │
 │ 500 mAh      │        │  • OV3660 camera         │         │ breakout     ├──► Pip-Boy
 │ (JST-SH 2p)  │        │  • USB host (native USB) │  VBUS   │ (Adafruit    │
 └──────┬───────┘        │                          │ ┌──────►│  5978)       │
        │                │  D3 ◄── Button ── 3V3    │ │       └──────────────┘
        │                │  D4 ──EN──┐              │ │
        │                └───────────┼──────────────┘ │
        │                      ┌─────▼──────┐         │
        └─────────────────────►│ 5V boost   ├─────────┘
                       VIN     │ (U1V11F5)  │ VOUT
                               └────────────┘
```

- **XIAO ESP32-S3 Sense:** runs the firmware, drives the camera, and acts as a **USB host**. The Pip-Boy shows up to it as a USB serial device (an Espruino console).
- **5 V boost regulator (Pololu U1V11F5):** turns the LiPo voltage into 5 V on the USB-C plug's VBUS, which powers the Pip-Boy's USB port. The XIAO switches it on and off with `D4` (`EN`). A 100 kΩ pull-down (R1, the R6 resistor taken off the regulator) keeps it off while the XIAO is asleep.
- **Shutter button:** connects `3V3` to `D3` (GPIO4). A press both takes a photo and wakes the XIAO from deep sleep.
- **Battery:** a 3.7 V 500 mAh TinyCircuits LiPo on the JST-SH connector (J3), wired to the XIAO's `BAT` pad. Plugging the XIAO's own USB-C into a charger charges it.

Each photo is captured at 480×320 grayscale, center-cropped to **400×208**, adjusted for brightness and contrast, and Floyd–Steinberg dithered to **2 bits per pixel**. It is then sent to the Pip-Boy as `IMGS/<timestamp>.img` using Espruino's packet file-transfer protocol.

## Repository layout

| Path | What it is |
| --- | --- |
| [Pip-Cam_Firmware/](Pip-Cam_Firmware/) | Arduino sketch for the XIAO ESP32-S3 Sense |
| [Pip-Cam_Firmware/Pip-Cam_Firmware.ino](Pip-Cam_Firmware/Pip-Cam_Firmware.ino) | Main state machine: USB host, capture, upload, sleep |
| [Pip-Cam_Firmware/ImageConverter.h](Pip-Cam_Firmware/ImageConverter.h) | Crop, brightness/contrast, and dithering to the `.img` format |
| [Pip-Cam_Firmware/camera_pins.h](Pip-Cam_Firmware/camera_pins.h) | Camera pin maps (`CAMERA_MODEL_XIAO_ESP32S3` is the one used) |
| [Pip-Cam_Board/](Pip-Cam_Board/) | KiCad projects |
| `Pip-Cam_MainBoard.*` | Main board (XIAO, connectors, R1) |
| `Pip-Cam_Button.*` | Small shutter-button board |
| `Pip-Cam_Panelized_Edge-to-board.*` | **Production panel:** main board and button board on one panel |
| [Pip-Cam_Board/production/](Pip-Cam_Board/production/) | Gerber zip, BOM, and pick-and-place files for fab and assembly |
| [Pip-Cam_Board/Fab/](Pip-Cam_Board/Fab/) | Loose Gerber and drill files for the panel |
| `*.step` / `*.stp` | 3D models of the boards and the off-board parts |
| [Pip-Cam_3D_Files/](Pip-Cam_3D_Files/) | STL files for the shell, the button plunger and the Pip-Boy adapters |
| [Pololu-R6.jpg](Pololu-R6.jpg) | Shows which resistor to take off the Pololu regulator to use as R1 |
| [Pip-Cam_Manuals.pdf](Pip-Cam_Manuals.pdf) | End-user manuals for the MK V and 3000 versions |
| [LICENSE](LICENSE), [LICENSES/](LICENSES/) | Which license covers what, and the full license texts |

## Bill of materials

| Qty | Part | Link |
| --- | --- | --- |
| 1 | Pip-Cam PCB (see [Ordering the PCBs](#ordering-the-pcbs)) | — |
| 1 | Seeed Studio **XIAO ESP32-S3 Sense** | [seeedstudio.com](https://www.seeedstudio.com/XIAO-ESP32S3-Sense-p-5639.html) |
| 1 | **Pololu U1V11F5**: 5 V step-up voltage regulator | [pololu.com/product/2562](https://www.pololu.com/product/2562) |
| 1 (2 for the 3000 adapter) | **Adafruit 5978**: USB Type-C plug breakout | [adafruit.com/product/5978](https://www.adafruit.com/product/5978) |
| 1 (3000 adapter only) | Female USB-C socket | [amazon.com/dp/B0B4J5NJ2Y](https://www.amazon.com/dp/B0B4J5NJ2Y) |
| 1 | TinyCircuits **lithium-ion polymer battery, 3.7 V 500 mAh** | [tinycircuits.com](https://tinycircuits.com/products/lithium-ion-polymer-battery-3-7v-500mah?variant=8736484327515) |
| 1 | JST **SM02B-SRSS-TB**: 2-pin 1.0 mm SH right-angle connector (battery) | [digikey.com](https://www.digikey.com/en/products/detail/jst-sales-america-inc/SM02B-SRSS-TB/926708) |
| 1 | Alps **SKQG** tact switch with stem (any SKQG works, e.g. SKQGABE010) | [digikey.com](https://www.digikey.com/en/products/detail/alps-alpine/SKQGABE010/19529161) |
| 1 | 1/16 in acrylic rod, cut to 5 mm (LED light pipe) | — |
| 1 | M3-0.5 × 8 mm screw (3000 adapter only) | — |
| 1 | M1-0.25 × 3 mm screw (closes the shell) | [mcmaster.com/91430A150](https://www.mcmaster.com/91430A150/) |
| 1 | M1-0.25 nut | [mcmaster.com/90591A311](https://www.mcmaster.com/90591A311/) |
| — | 3D-printed parts (see [3D-printed parts](#3d-printed-parts)) | [Pip-Cam_3D_Files/](Pip-Cam_3D_Files/) |

R1 (100 kΩ) is not a separate purchase: it is taken off the Pololu regulator (see [Assembly](#assembly)).

### 3D-printed parts

| File | Needed for | Purpose |
| --- | --- | --- |
| [Shell_Left.stl](Pip-Cam_3D_Files/Shell_Left.stl) | All | Left shell half. Holds the light pipe. |
| [Shell_Right.stl](Pip-Cam_3D_Files/Shell_Right.stl) | All | Right shell half. Has the slot for the M1 nut on the inside. |
| [Button_Plunger.stl](Pip-Cam_3D_Files/Button_Plunger.stl) | All | Shutter button plunger that presses S1 |
| [MKV_Adaptor.stl](Pip-Cam_3D_Files/MKV_Adaptor.stl) | MK V | Adapter for the Pip-Boy MK V |
| [3000_AdapterTop.stl](Pip-Cam_3D_Files/3000_AdapterTop.stl) | 3000 | Top half of the Pip-Boy 3000 adapter |
| [3000_AdapterBottom.stl](Pip-Cam_3D_Files/3000_AdapterBottom.stl) | 3000 | Bottom half of the Pip-Boy 3000 adapter |
| [3000_AdapterBottom_Spacer_Version.stl](Pip-Cam_3D_Files/3000_AdapterBottom_Spacer_Version.stl) | 3000 | Use instead of `3000_AdapterBottom` if your Pip-Boy has the cuff spacer fitted |

### Where each part goes on the PCB

| Ref | Part | Footprint / connection | Notes |
| --- | --- | --- | --- |
| U1 | XIAO ESP32-S3 Sense | XIAO-ESP32S3-SMD | Must be the *Sense* (camera + PSRAM). Soldered flat as an SMD module, using its underside USB D+/D- and BAT pads. |
| R1 | 100 kΩ resistor (R6 from the Pololu U1V11F5) | 0603 | Pull-down on the boost `EN` line |
| S1 | Alps SKQG tact switch with stem | SW_SPST_SKQG_WithStem | Shutter button, on the button board |
| J1 | Pololu U1V11F5 | 1×4, 2.54 mm | 1 `EN` · 2 `VIN` (BAT) · 3 `GND` · 4 `VOUT` |
| J2 | Adafruit 5978 | 1×5, 2.54 mm | 1 `VBUS` (VOUT) · 2 n/c · 3 `D+` · 4 `D-` · 5 `GND` |
| J3 | SM02B-SRSS-TB | SM02BSRSSTBLFSN | Battery connector. Pin 1 = GND, pin 2 = BAT. |

## Ordering the PCBs

The board is a 2-layer, 1.6 mm FR-4 design. The panel `Pip-Cam_Panelized_Edge-to-board` has the main board and the button board together. The production files were generated with the KiCad *Fabrication Toolkit* plugin in a JLCPCB-ready format.

1. Upload [Pip-Cam_Board/production/Pip-Cam_Panelized_Edge-to-board.zip](Pip-Cam_Board/production/Pip-Cam_Panelized_Edge-to-board.zip) as the Gerber file. Order a **stencil** with it: R1 and the XIAO are reflowed with solder paste.
2. *(Optional, for PCB assembly)* Upload [bom.csv](Pip-Cam_Board/production/bom.csv) and [positions.csv](Pip-Cam_Board/production/positions.csv). The BOM has no LCSC part numbers, so you will have to match the parts by hand. Normally all three parts are hand-placed, and R1 comes off the Pololu regulator, so assembly from the fab is not needed.
3. To regenerate the outputs after changing the design, open `Pip-Cam_Panelized_Edge-to-board.kicad_pro` in KiCad 8+ and run Fabrication Toolkit. Its settings are in [fabrication-toolkit-options.json](Pip-Cam_Board/fabrication-toolkit-options.json).

## Assembly

1. **Separate the panel** into the main board and the button board, and clean up the break-off tabs.
2. **Take R6 off the Pololu U1V11F5.** Put the regulator on a hot plate and remove the 100 kΩ resistor marked **R6** in [Pololu-R6.jpg](Pololu-R6.jpg) (printed `01D`). Keep it: it becomes R1.
3. **Reflow R1 and the XIAO together on a hot plate.**
   > ⚠️ **Remove the camera module (the Sense expansion board) from the XIAO before soldering.** Heating the XIAO with the camera fitted is dangerous. Attach the camera only after all soldering is done (step 8).

   Apply solder paste to the main board with the stencil. Place R1 (the resistor from step 2) and the bare XIAO ESP32-S3 on their footprints, then reflow both on the hot plate in one pass. The XIAO's **underside pads** must reflow as well as its castellated edges, because the USB `D+`/`D-` and `BAT` pads carry signals on this board.

   S1 (the SKQG switch) on the button board can be reflowed on the hot plate too, or soldered with an iron later in step 7.
4. **J3 (battery connector).** Solder J3 on the main board **with an iron**. It can't go on the hot plate.
5. **5 V boost regulator (J1).** Mount the Pololu U1V11F5 (with R6 already removed) using the header pins that come with it, matching `EN`, `VIN`, `GND` and `VOUT` to the silkscreen:
   1. Push the header through the main board so the black plastic spacer is on the side **opposite** the regulator (same side as the XIAO). The spacer holds the pins straight.
   2. Put the regulator on the pins on the other side, flush against the main board, and solder the **regulator** side.
   3. Pull the black spacer off the pins.
   4. Solder the **main board** side and trim the excess pins.
6. **USB-C plug (J2).** Mount the Adafruit 5978 breakout using the header that comes with it. This works the opposite way to the regulator, with the breakout soldered last:
   1. Put the header, with its black spacer, through the breakout. Push the pins through the main board.
   2. Solder the pins on the **main board** side, opposite the breakout.
   3. Pull the black spacer off, press the breakout flush, and solder the **breakout** side.
7. **Button board (do this last, before plugging in the battery).**
   - If S1 wasn't reflowed in step 3, solder it now with an iron.
   - The fingers on the button board slot into the main board. They often need **filing** before they fit.
   - Fit the button board so the switch faces **up**, the same direction as the XIAO's USB-C port.
   - Solder its two pads to the main board.
8. **Attach the camera.** Once all soldering is finished and the board has cooled, fit the camera module (Sense expansion board) back onto the XIAO.
9. **Battery and test.** Plug the TinyCircuits 500 mAh LiPo into J3. J3 expects pin 1 = **GND** and pin 2 = **BAT+**. Check the polarity with a meter first, especially if you use a different battery: JST-SH batteries are sold wired both ways. Then check, before closing the case:
   - The XIAO's orange LED should blink at about 1 Hz for 10 s while it looks for the Pip-Boy, then stop blinking (deep sleep). Pressing the shutter button should wake it again.
   - While it is awake, VBUS on the USB-C plug should read about 5 V. While it is asleep, it should read about 0 V.
10. **Light pipe.** Ream the light-pipe hole in `Shell_Left` to size with a small drill bit. Glue in the 5 mm piece of 1/16 in acrylic rod.
11. **Close the shell.** Fit the button plunger and the electronics into the shell halves. Put the M1 nut in the slot on the inside of `Shell_Right`, then screw the halves together at the bottom with the M1 × 3 mm screw.
12. **Adapter.**
    - **MK V:** use `MKV_Adaptor`.
    - **3000:** wire the female USB-C socket to the second Adafruit USB-C plug breakout, then place both in the adapter. Assemble `3000_AdapterTop` and `3000_AdapterBottom` (or the spacer version) with the M3 × 8 mm screw.

    Then attach the Pip-Cam as shown in [Pip-Cam_Manuals.pdf](Pip-Cam_Manuals.pdf).

## Programming the firmware

### 1. Install the toolchain

1. Install the **Arduino IDE 2.x**.
2. In *Boards Manager*, install **esp32 by Espressif Systems**, version **3.x**. The USB host library depends on ESP-IDF 5.
3. Install the **USBHostSerial** library by Bert Melis ([github.com/bertmelis/USBHostSerial](https://github.com/bertmelis/USBHostSerial)), either from the Library Manager or as a ZIP via *Sketch → Include Library → Add .ZIP Library*.
   `esp_camera` and `mbedtls` come with the ESP32 core.

### 2. Board settings (*Tools* menu)

| Setting | Value |
| --- | --- |
| Board | **XIAO_ESP32S3** |
| PSRAM | **OPI PSRAM** (required: the frame buffer lives in PSRAM) |
| Flash Size | 8MB |
| Partition Scheme | Default |
| USB CDC On Boot | Enabled |

### 3. Upload

1. Open [Pip-Cam_Firmware/Pip-Cam_Firmware.ino](Pip-Cam_Firmware/Pip-Cam_Firmware.ino).
2. Unplug the battery, then connect the XIAO's **own** USB-C port to your computer.
3. Put the XIAO into the bootloader: **hold BOOT, plug in the USB, then release BOOT.**
   This step is needed because the firmware takes over the ESP32-S3's native USB for host mode and also deep-sleeps most of the time. The port it would normally show up on doesn't exist while the firmware is running.
4. Select the port and click **Upload**.
5. Unplug the XIAO and plug in the battery, the orange light should start to blink.

The firmware has no serial logging, because the USB port is busy talking to the Pip-Boy. Use the orange user LED to see what it is doing:

| LED | Meaning |
| --- | --- |
| Blinking ~1 Hz | Awake, waiting for the Pip-Boy over USB (gives up after 10 s) |
| Steady or off (not blinking) | Connected and idle, or in deep sleep |
| Fast flicker | Uploading a photo to the Pip-Boy |

### Tunables

These are at the top of `Pip-Cam_Firmware.ino`:

| Constant | Default | Purpose |
| --- | --- | --- |
| `CONVERT_BPP` | `2` | Output bit depth (1, 2, 4 or 8). The Pip-Boy app expects 2. |
| `CONVERT_BRIGHTNESS` / `CONVERT_CONTRAST` | `0` | Defaults used when the Pip-Boy sends no values |
| `SLEEP_TIMEOUT` | `60000` ms | Idle time while connected before deep sleep |
| USB connect timeout (in `STATE_WAIT_USB`) | `10000` ms | How long to look for the Pip-Boy after waking |
| `ACK_TIMEOUT` | `2000` ms | Upload is abandoned if the Pip-Boy doesn't ACK a packet in time |

The firmware writes OV3660 registers directly (`0x3008` software power-down). The camera is also flipped vertically (`set_vflip`) to suit how it is mounted in the shell. If your XIAO Sense came with the older OV2640 sensor, those register writes do nothing. Capture still works, but standby current will be higher.

## Installing the Pip-Boy app

The Pip-Boy needs the Pip-Cam app, which handles the button events and the photo viewer. Install it with the **Pip-Cam Module Installer**: scan the QR code in [Pip-Cam_Manuals.pdf](Pip-Cam_Manuals.pdf) and open the link **on a computer**. The installer connects to the Pip-Boy over USB from the browser.

The Pip-Boy app source is in a separate repository, [AidansLab/Pip-Boy-CFW-Builder](https://github.com/AidansLab/Pip-Boy-CFW-Builder):

| Pip-Boy | App source |
| --- | --- |
| Pip-Boy 3000 | [`/camera`](https://github.com/AidansLab/Pip-Boy-CFW-Builder/tree/main/camera) |
| Pip-Boy MK V | [`/Patches`](https://github.com/AidansLab/Pip-Boy-CFW-Builder/tree/main/Patches) |

## Using the Pip-Cam

1. Plug the Pip-Cam into the Pip-Boy.
   - **Pip-Boy 3000:** plug in through the adapter. When removing it, **pull out the adapter with the Pip-Cam still attached, then detach the Pip-Cam**.
2. Press the shutter button to wake the camera. The XIAO powers the Pip-Boy's USB port, finds the Pip-Boy, and the Pip-Cam footer appears.
3. Open the Pip-Cam app and take photos with the shutter button.

| Control | MK V | 3000 |
| --- | --- | --- |
| Capture photo | Shutter button on the Pip-Cam | Shutter button on the Pip-Cam |
| Brightness | Scroll left wheel | Capture mode: scroll left wheel |
| Reconvert last photo with new settings | Click left wheel | Capture mode: click left wheel |
| Contrast | Right-side knob | Capture mode: right-side knob |
| Next / previous image | Right wheel up/down | Photos mode: right-side knob |
| Delete menu | Click right wheel | Photos mode: click left wheel (scroll to choose) |
| Switch mode | — | Hold left wheel |
| Exit app | Power button | Power button |

> **Note:** Reconvert only works on the **latest** photo, not on older ones in the gallery. The Pip-Cam keeps only the most recent raw capture, in RAM, and re-dithers it with the new brightness and contrast. That capture is lost when the Pip-Cam goes to sleep, so re-tune a photo before the camera times out.

The camera goes back to deep sleep after 60 s with no activity. Press the shutter button to wake it.

## Troubleshooting

| Symptom | Likely cause |
| --- | --- |
| Arduino can't find a port or upload fails | Enter the bootloader: hold BOOT while plugging in the USB |
| LED blinks for 10 s, then stops, and the Pip-Boy shows nothing | No USB link. Check the J2 header joints on both the main board and the breakout (D+ and D-), that VBUS reaches about 5 V while awake, and the J1 regulator joints (especially `EN`). |
| Nothing at all when pressing the button | Battery flat or connected backwards (check J3 polarity), or the button board isn't connected to D3/3V3 |
| Photo upside down | Remove or flip `s->set_vflip(s, 1)` in `setup()` to suit your mounting |
| Camera init fails or no photo | PSRAM not set to **OPI PSRAM**, or the camera module isn't fully seated on the XIAO |
| Upload stops partway | The Pip-Boy didn't ACK within 2 s. Check the SD card has free space and that the Pip-Cam app is installed. |

## License

| Part | License |
| --- | --- |
| Firmware ([Pip-Cam_Firmware/](Pip-Cam_Firmware/)) | [GPL-3.0-or-later](LICENSES/GPL-3.0.txt) |
| Hardware ([Pip-Cam_Board/](Pip-Cam_Board/), [Pip-Cam_3D_Files/](Pip-Cam_3D_Files/)) and documentation | [CERN-OHL-S-2.0](LICENSES/CERN-OHL-S-2.0.txt) |

Third-party 3D models and images are excluded. See [LICENSE](LICENSE) for details.

Support: contact@aidan.calamera.com
