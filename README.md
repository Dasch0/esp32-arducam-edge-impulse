# ESP32 + Arducam Mini 2MP Plus Edge Impulse Example

## Overview
This project runs an Edge Impulse designed image classification impulse on an ESP32 dev kit. These images are captured using a connected ArduCAM Mini 2MP Plus camera board.

This project is intended to work within the Arduino framework using PlatformIO. It is also compatible with the Arduino IDE. If you would like to use the Arduino IDE/CLI to build this project, See the FAQ[#faq] section for more information.

## Prerequisites
- You have an ESP32 dev kit
- You have an ArduCAM Mini 2MP Plus board
- You have jumper wires to connect the ArduCAM to the correct ESP32 pins
- (Optional) You've created an Edge Impulse account, and followed one of the image classification tutorial to understand how to train machine learning model with Edge Impulse:
  - [adding sight to your sensors](https://docs.edgeimpulse.com/docs/image-classification) 
- You've installed [PlatfomIO](https://platformio.org/install) - the IDE, CLI, or vscode extension all work equally well here

## Hardware Setup

Before running this project, the ArduCAM must be connected to the ESP32 dev kit. To do this:

1. Review the pin-out diagram for your specific ESP32 dev kit. There are multiple kit variations for ESP32. For instance for the 30-pin 'DOIT DEVIT V1 ESP32-WROOM-32', the pinout is shown below:
![RNT ESP32 Pinout](https://i0.wp.com/randomnerdtutorials.com/wp-content/uploads/2018/08/ESP32-DOIT-DEVKIT-V1-Board-Pinout-30-GPIOs-Copy.png?w=966&quality=100&strip=all&ssl=1)

2. Using the jumper wires, connect the following pins on the ESP32 kit to the ArduCAM in the order they appear. Ensure that the ESP32 is not powered during this

|ESP32 |ArduCAM|Description        |
|:-----|:------|:------------------|
|Pin 5 | CS    |Chip Select for SPI|
|Pin 23| MOSI  |SPI MOSI           |
|Pin 19| MISO  |SPI MISO           |
|Pin 18| SCK   |SPI Clock          |
|Pin 21| SDA   |I2C Data           |
|Pin 22| SCL   |I2C Clock          |
|GND   | GND   |Common ground      |
|VCC   | VCC   |5V connection pin  | 

Note: VCC may be labelled '5V' on some ESP32 kits

3. Double check that the pin connections are correct, and then connect the ESP32 to your computer via the ESP32's micro-USB port. We will verify that the hardware is connected correctly when testing out the firmware

## Software Setup

Assuming PlatformIO is set up correctly, all dependencies should be fetched automatically based on the information in the platformio.ini file when the project is built.

If you are using the platformIO CLI you can build, program, and monitor the serial output from the ESP32 using:
```
platformio run --target upload --target monitor --environment esp32dev
```

You may need to press the `BOOT` button on the ESP32 kit when you are prompted with
```
Connecting........_
```
to get the board to program. After the board is finished programming, the code will begin taking photographs, and classifying the image with the default included impulse.

If you are using the IDE/VScode, see the [PlatformIO docs](https://docs.platformio.org/en/latest/tutorials/espressif32/arduino_debugging_unit_testing.html#compiling-and-uploading-the-firmware) on including and building the project. Select 'Upload and Monitor' when building.

## Verify program output

After programming, the first thing you should see is initialization status messages:
```
Serial Interface Initialized.
SPI initialized.
I2C initialized.
Camera initialized.
```

During these steps some checks are performed to make sure the ArduCAM is working properly. If errors occur here, the program will stall until they are cleared. These errors will generally be due to some hardware fault. First triple check the [hardware connections](#hardware-setup), then check the [FAQ](#faq) for more troubleshooting information.

If initialization is successful, the program will now count down, take a photo, and classify the output in a loop:
```
taking a photo in 3... 2... 1...
*click*
run_classifier returned: 0
Predictions (DSP: 4 ms., Classification: 1999 ms., Anomaly: 0 ms.):
[0.95312, 0.04688]
    non_person: 0.95312
    person: 0.04688
```

The default loaded model is a simple classifer that classifies if a person is present in the captured image. The captured image is at 96x96 resolution, and the pre-loaded network is calibrated such that values >0.4 indicate a person is likely present.

## Adding your own model
It is very straightfoward to replace the pre-loaded model with other edge-impulse projects. Head over to your Edge Impulse project (created in one of the tutorials linked above), and go to Deployment. From here you can create the full library which contains the impulse and all external required libraries. Select Arduino library and click Build to create the library. Then download the .zip file, and place it in the `./lib/` directory of this repository.

Open the `platformio.ini` file in the root directory of this repository, and replace **line 16** to point to your newly downloaded .zip file library:
```
15 lib_deps = 
16 	./lib/edge-impulse-library.zip # change this line to the name of your edge-impulse library
```

Next, open `src/main.cpp` and on **line 2**, change the top level edge-impulse library header file to match the name of your Edge Impulse project:

```
2 #include <Person_Detection_Classification__inferencing.h>
```

If you aren't sure what the header name for your project would be, peek inside the .zip file library and copy the name of the single header file in the root directory.

## FAQ
