#ifndef TEST_02_BUTTON_SKETCH_CONFIG_H
#define TEST_02_BUTTON_SKETCH_CONFIG_H

// Board selection:
//   Current build targets AIPI Lite.
//   Comment this line out to target Freenove ESP32-S3 Media Kit instead.
//   Alternatively, remove this local define and pass -DBOARD_AIPI_LITE to
//   both C and C++ compiler flags when building AIPI.
#define BOARD_AIPI_LITE

#ifndef ARDUINO_USB_CDC_ON_BOOT
#define ARDUINO_USB_CDC_ON_BOOT 1
#endif

#ifndef ARDUINO_USB_MODE
// ESP32-S3: 1 = USB-Serial/JTAG.
#define ARDUINO_USB_MODE 1
#endif

#ifndef ARDUINO_USB_MSC_ON_BOOT
#define ARDUINO_USB_MSC_ON_BOOT 0
#endif

#ifndef ARDUINO_USB_DFU_ON_BOOT
#define ARDUINO_USB_DFU_ON_BOOT 0
#endif

#endif
