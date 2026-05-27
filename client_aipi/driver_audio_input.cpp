#include "driver_audio_input.h"

static I2SClass i2s_input;

void audio_input_init(uint8_t mclk, uint8_t ws, uint8_t din) {
    // AIPI-Lite: Configure I2S input from ES8311 codec
    // MCLK=6, WS=12, DIN=13 (BCLK=14 shared with output, configured separately)
    // Note: BCLK needs to be set via the I2S begin call, not setPins
    i2s_input.setPins(-1, ws, -1, din);  // No BCLK here; it's implicitly handled by I2S

    // Use 16kHz to match backend and avoid downsampling artifacts
    if (!i2s_input.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
        Serial.println("Failed to initialize I2S input bus!");
        return;
    }
    Serial.println("I2S input bus initialized at 16kHz (ES8311 codec mode).");
}

void audio_input_deinit(void)
{ 
    i2s_input.end(); 
}

uint8_t* audio_input_record_wav(uint32_t duration, size_t *wav_size) {
    return i2s_input.recordWAV(duration, wav_size);
}

void audio_input_print_buffer(uint8_t* buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        Serial.print(buffer[i]);
        Serial.print(" ");
    }
    Serial.println();
}

size_t audio_input_read_iis_data(char* buffer, size_t size) {
    return i2s_input.readBytes(buffer, size);
}

int audio_input_get_iis_data_available(void) {
    return i2s_input.available();
}
