#include <JPEGDecoder.h>
#include <Person_Detection_Classification__inferencing.h>
#include <ArduCAM.h>
#include <Wire.h>
#include <SPI.h>

// raw frame buffer from the camera
#define FRAME_BUFFER_COLS           96 
#define FRAME_BUFFER_ROWS           96 

// size of the cutout to feed to Edge Impulse
#define CUTOUT_COLS                 EI_CLASSIFIER_INPUT_WIDTH
#define CUTOUT_ROWS                 EI_CLASSIFIER_INPUT_HEIGHT
const int cutout_row_start = (FRAME_BUFFER_ROWS - CUTOUT_ROWS) / 2;
const int cutout_col_start = (FRAME_BUFFER_COLS - CUTOUT_COLS) / 2;

// Buffer to store captured jpegs and framebuffers
char jpeg_buffer[4096];
uint16_t pixel_buffer[FRAME_BUFFER_COLS * FRAME_BUFFER_ROWS];

// ArduCAM driver handle with CS pin defined as pin 5 for ESP32 dev kits
const int CS = 5;
ArduCAM myCAM(OV2640, CS);

// Initializes the I2C interface to the camera
//  The code after Wire.begin() isn't strictly necessary, but it verifies that the I2C interface is
//  working correctly and that the expected OV2640 camera is connected.
void arducam_i2c_init() {
  // Initialize I2C bus
  Wire.begin();

  // Verify the I2C bus works properly, and the ArduCAM vid and pid registers match their
  // expected values
  uint8_t vid, pid;
  while(1) {
    myCAM.wrSensorReg8_8(0xff, 0x01);
    myCAM.rdSensorReg8_8(OV2640_CHIPID_HIGH, &vid);
    myCAM.rdSensorReg8_8(OV2640_CHIPID_LOW, &pid);
    if ((vid != 0x26 ) && (( pid != 0x41 ) || ( pid != 0x42 ))) {
      Serial.println(F("I2C error!"));
      delay(1000);
      continue;
    }
    else {
      Serial.println(F("I2C initialized."));
      break;
    } 
  }
}

// Initializes the SPI interface to the camera
//  The code after SPI.begin() isn't strictly necessary, but it implements some workarounds to
//  common problems and verifies that the SPI interface is working correctly
void arducam_spi_init() {
  // set the CS pin as an output:
  pinMode(CS, OUTPUT);
  digitalWrite(CS, HIGH);
  // initialize SPI:
  SPI.begin();

  // Reset the CPLD register (workaround for intermittent spi errors)
  myCAM.write_reg(0x07, 0x80);
  delay(100);
  myCAM.write_reg(0x07, 0x00);
  delay(100);

  // Check if the ArduCAM SPI bus is OK
  while (1) {
    myCAM.write_reg(ARDUCHIP_TEST1, 0x55);
    uint8_t temp = myCAM.read_reg(ARDUCHIP_TEST1);
    if (temp != 0x55) {
      Serial.println(F("SPI error!"));
      delay(1000);
      continue;
    } else {
      Serial.println(F("SPI initialized."));
      break;
    }
  }
}

// Initializes the camera driver and hardware with desired settings. Should run once during setup()
void arducam_init() {
    arducam_spi_init();
    arducam_i2c_init();
    // set to JPEG format, this works around issues with the color data when sampling in RAW formats
    myCAM.set_format(JPEG);
    myCAM.InitCAM();
    // Specify the smallest possible resolution
    myCAM.OV2640_set_JPEG_size(OV2640_160x120);
    delay(100);
    Serial.println(F("Camera initialized."));
}

// Capture a photo on the camera. This method only takes the photograph, call arducam_transfer() to
// read get the data 
void arducam_capture() {
  // Make sure the buffer is emptied before each capture
  myCAM.flush_fifo();
  myCAM.clear_fifo_flag();
  // Start capture
  myCAM.start_capture();
  // Wait for indication that it is done
  while (!myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK));
  delay(50);
  myCAM.clear_fifo_flag();
}

// Transfer the last captured jpeg from the arducam to a buffer. The buffer must be larger than the
// captured jpeg. Returns the length of the jpeg file in bytes
uint32_t arducam_transfer(char buf[], uint32_t buf_len) {
  uint32_t jpeg_length = myCAM.read_fifo_length();
  // verify buffer can fit the jpeg
  if (jpeg_length > buf_len) {
    Serial.println(F("Error: buffer not large enough to hold image"));
    return 0;
  }
  myCAM.CS_LOW();
  myCAM.set_fifo_burst();
  for (int index = 0; index < jpeg_length; index++) {
    buf[index] = SPI.transfer(0x00);
  }
  delayMicroseconds(15);
  myCAM.CS_HIGH();

  return jpeg_length;
}

// Decode, crop, and store jpeg data to RGB565 pixel buffer
// Note: Buffer must be width * height sized
//
// Implementation borrowed heavily from:
//   https://github.com/antmicro/tensorflow-arduino-examples/blob/master/tflite-micro/examples/person_detection/arduino_image_provider.cpp
void jpeg_store_data(char *jpeg_buf, uint16_t *out_buf, size_t width, size_t height) {
  // Parse the JPEG headers. The image will be decoded as a sequence of Minimum
  // Coded Units (MCUs), which are 16x8 blocks of pixels.
  JpegDec.decodeArray((const uint8_t *)jpeg_buffer, 4096);

  // Crop the image by keeping a certain number of MCUs in each dimension
  const int keep_x_mcus = width / JpegDec.MCUWidth;
  const int keep_y_mcus = height / JpegDec.MCUHeight;

  // Calculate how many MCUs we will throw away on the x axis
  const int skip_x_mcus = JpegDec.MCUSPerRow - keep_x_mcus;
  // Roughly center the crop by skipping half the throwaway MCUs at the
  // beginning of each row
  const int skip_start_x_mcus = skip_x_mcus / 2;
  // Index where we will start throwing away MCUs after the data
  const int skip_end_x_mcu_index = skip_start_x_mcus + keep_x_mcus;
  // Same approach for the columns
  const int skip_y_mcus = JpegDec.MCUSPerCol - keep_y_mcus;
  const int skip_start_y_mcus = skip_y_mcus / 2;
  const int skip_end_y_mcu_index = skip_start_y_mcus + keep_y_mcus;
  uint16_t *pImg;
  while (JpegDec.read()) {
    // Skip over the initial set of rows
    if (JpegDec.MCUy < skip_start_y_mcus) {
      continue;
    }
    // Skip if we're on a column that we don't want
    if (JpegDec.MCUx < skip_start_x_mcus ||
        JpegDec.MCUx >= skip_end_x_mcu_index) {
      continue;
    }
    // Skip if we've got all the rows we want
    if (JpegDec.MCUy >= skip_end_y_mcu_index) {
      continue;
    }
    // Pointer to the current pixel
    pImg = JpegDec.pImage;

    // The x and y indexes of the current MCU, ignoring the MCUs we skip
    int relative_mcu_x = JpegDec.MCUx - skip_start_x_mcus;
    int relative_mcu_y = JpegDec.MCUy - skip_start_y_mcus;

    // The coordinates of the top left of this MCU when applied to the output
    // image
    int x_origin = relative_mcu_x * JpegDec.MCUWidth;
    int y_origin = relative_mcu_y * JpegDec.MCUHeight;

    // Loop through the MCU's rows and columns
    for (int mcu_row = 0; mcu_row < JpegDec.MCUHeight; mcu_row++) {
      // The y coordinate of this pixel in the output index
      int current_y = y_origin + mcu_row;
      for (int mcu_col = 0; mcu_col < JpegDec.MCUWidth; mcu_col++) {
        // Read the color of the pixel as 16-bit integer
        uint16_t color = *pImg++;
        //calculate index
        int current_x = x_origin + mcu_col;
        size_t index = (current_y * width) + current_x;
        // store the RGB565 pixel to the buffer
        out_buf[index] = color;
      }
    }
  }
}

// Convert RBG565 pixels into RBG888 pixels
void r565_to_rgb(uint16_t color, uint8_t *r, uint8_t *g, uint8_t *b) {
    *r = (color & 0xF800) >> 8;
    *g = (color & 0x07E0) >> 3;
    *b = (color & 0x1F) << 3;
}


// Data ingestion helper function for grabbing pixels from a framebuffer into Edge Impulse
// This method should be used as the .get_data callback of a signal_t 
int cutout_get_data(size_t offset, size_t length, float *out_ptr) {
    // so offset and length naturally operate on the *cutout*, so we need to cut it out from the real framebuffer
    size_t bytes_left = length;
    size_t out_ptr_ix = 0;
    
    // read byte for byte
    while (bytes_left != 0) {
        // find location of the byte in the cutout
        size_t cutout_row = floor(offset / CUTOUT_COLS);
        size_t cutout_col = offset - (cutout_row * CUTOUT_COLS);

        // then read the value from the real frame buffer
        size_t frame_buffer_row = cutout_row + cutout_row_start;
        size_t frame_buffer_col = cutout_col + cutout_col_start;
        
        uint16_t pixelTemp = pixel_buffer[(frame_buffer_row * FRAME_BUFFER_COLS) + frame_buffer_col];

        uint16_t pixel = (pixelTemp>>8) | (pixelTemp<<8);

        uint8_t r, g, b;
        r565_to_rgb(pixel, &r, &g, &b);
        float pixel_f = (r << 16) + (g << 8) + b;
        out_ptr[out_ptr_ix] = pixel_f;
  
        out_ptr_ix++;
        offset++;
        bytes_left--;
    }

    // and done!
    return 0;
}

void setup()
{
  // Set up the serial connection for printing to terminal
  Serial.begin(115200);
  Serial.println("Serial Interface Initialized."); 

  arducam_init();
}

void loop()
{
  Serial.println();
  Serial.print(F("taking a photo in 3... "));
  delay(1000);
  Serial.print(F("2... "));
  delay(1000);
  Serial.print(F("1..."));
  delay(1000);
  Serial.println();
  Serial.println(F("*click*"));

  // Take the photo
  arducam_capture();

  // Transfer the photo to jpeg buffer 
  uint32_t jpeg_size = arducam_transfer(jpeg_buffer, 4096);

  // decode and crop jpeg into pixel_buffer
  jpeg_store_data(jpeg_buffer, pixel_buffer, CUTOUT_COLS, CUTOUT_ROWS);
  
  // create signal that inputs pixel buffer to Impulse
  signal_t signal;
  signal.total_length = CUTOUT_COLS * CUTOUT_ROWS;
  signal.get_data = &cutout_get_data;

  // Run the neural network and get the prediction 
  ei_impulse_result_t result = { 0 };
  EI_IMPULSE_ERROR res = run_classifier(&signal, &result, false /* debug */);
  ei_printf("run_classifier returned: %d\n", res);

  if (res != 0) return;

  // print the predictions
  ei_printf("Predictions ");
  ei_printf("(DSP: %d ms., Classification: %d ms., Anomaly: %d ms.)",
      result.timing.dsp, result.timing.classification, result.timing.anomaly);
  ei_printf(": \n");
  ei_printf("[");
  for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
      ei_printf("%.5f", result.classification[ix].value);
#if EI_CLASSIFIER_HAS_ANOMALY == 1
      ei_printf(", ");
#else
      if (ix != EI_CLASSIFIER_LABEL_COUNT - 1) {
          ei_printf(", ");
      }
#endif
  }
#if EI_CLASSIFIER_HAS_ANOMALY == 1
  ei_printf("%.3f", result.anomaly);
#endif
  ei_printf("]\n");

  // human-readable predictions
  for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
      ei_printf("    %s: %.5f\n", result.classification[ix].label, result.classification[ix].value);
  }
#if EI_CLASSIFIER_HAS_ANOMALY == 1
  ei_printf("    anomaly score: %.3f\n", result.anomaly);
#endif

}

// Edge Impulse standardized print method, used for printing results after inference
void ei_printf(const char *format, ...) {
    static char print_buf[1024] = { 0 };

    va_list args;
    va_start(args, format);
    int r = vsnprintf(print_buf, sizeof(print_buf), format, args);
    va_end(args);

    if (r > 0) {
        Serial.write(print_buf);
    }
}
