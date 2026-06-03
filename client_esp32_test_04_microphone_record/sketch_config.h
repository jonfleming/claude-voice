#ifndef TEST_04_MICROPHONE_RECORD_SKETCH_CONFIG_H
#define TEST_04_MICROPHONE_RECORD_SKETCH_CONFIG_H

// Test 02 is currently configured for AIPI Lite.
// Comment this out to build the same sketch for Freenove.
#define BOARD_AIPI_LITE

#ifndef ARDUINO_USB_CDC_ON_BOOT
#define ARDUINO_USB_CDC_ON_BOOT 1
#endif

#ifndef ARDUINO_USB_MODE
#define ARDUINO_USB_MODE 1
#endif

#ifndef ARDUINO_USB_MSC_ON_BOOT
#define ARDUINO_USB_MSC_ON_BOOT 0
#endif

#ifndef ARDUINO_USB_DFU_ON_BOOT
#define ARDUINO_USB_DFU_ON_BOOT 0
#endif

#endif
