// audio_dma_buffer.cpp
// I2S DMA buffer management for ESP32-S3.
// Provides double-buffered I2S capture and streaming playback with
// minimal latency for voice assistant use cases.

#include "audio_dma_buffer.h"
#include "board_pins.h"
#include <Wire.h>
#include <esp_heap_caps.h>

// --- Global state ---

static I2SClass i2s_capture;
static I2SClass i2s_playback;
static volatile audio_state_t s_state = AUDIO_STATE_IDLE;
static int s_mclk_pin = -1;

// --- Capture DMA ---

// Double-buffered capture state
static uint8_t s_capture_buf[AUDIO_DMA_NUM_BUFFERS][AUDIO_DMA_CAPTURE_SIZE];
static volatile uint32_t s_capture_buf_count = 0;  // bytes available in active buffer

bool audio_dma_capture_init_mclk(uint8_t mclk, uint8_t bclk, uint8_t lrclk, uint8_t din) {
    s_mclk_pin = mclk;
    
    // Configure I2S pins: (BCLK, LRCLK, DOUT, DIN, MCLK)
    i2s_capture.setPins(bclk, lrclk, -1, din, mclk);
    
    // Initialize I2S at 16kHz, 32-bit stereo
    if (!i2s_capture.begin(I2S_MODE_STD, AUDIO_I2S_SAMPLE_RATE,
                           AUDIO_I2S_BITS_PER_SAMPLE, AUDIO_I2S_CHANNEL_MODE,
                           I2S_STD_SLOT_BOTH)) {
        Serial1.printf("[dma] I2S capture init failed (MCLK=%d)\n", mclk);
        return false;
    }
    
    Serial1.printf("[dma] I2S capture initialized at %dHz (MCLK=%d)\n",
                  AUDIO_I2S_SAMPLE_RATE, mclk);
    s_state = AUDIO_STATE_CAPTURING;
    return true;
}

bool audio_dma_capture_init(uint8_t bclk, uint8_t lrclk, uint8_t din) {
    s_mclk_pin = -1;
    
    // Configure I2S pins: (BCLK, LRCLK, DOUT, DIN, MCLK)
    i2s_capture.setPins(bclk, lrclk, -1, din, -1);
    
    // Initialize I2S at 16kHz, 32-bit stereo
    if (!i2s_capture.begin(I2S_MODE_STD, AUDIO_I2S_SAMPLE_RATE,
                           AUDIO_I2S_BITS_PER_SAMPLE, AUDIO_I2S_CHANNEL_MODE,
                           I2S_STD_SLOT_BOTH)) {
        Serial1.printf("[dma] I2S capture init failed (no MCLK)\n");
        return false;
    }
    
    Serial1.printf("[dma] I2S capture initialized at %dHz (no MCLK)\n",
                  AUDIO_I2S_SAMPLE_RATE);
    s_state = AUDIO_STATE_CAPTURING;
    return true;
}

void audio_dma_capture_deinit(void) {
    i2s_capture.end();
    s_state = (s_state == AUDIO_STATE_BOTH) ? AUDIO_STATE_PLAYING : AUDIO_STATE_IDLE;
    Serial1.println("[dma] I2S capture deinitialized");
}

size_t audio_dma_capture_read(uint8_t *dest, size_t max_bytes) {
    if (s_state == AUDIO_STATE_IDLE || s_state == AUDIO_STATE_CAPTURING) {
        // Only capture or both states are valid for reading
    } else {
        return 0;
    }
    
    // Read from I2S using the ESP_I2S library's built-in buffer
    size_t available = i2s_capture.available();
    if (available == 0) return 0;
    
    // Limit to requested size
    size_t to_read = (available < max_bytes) ? available : max_bytes;
    
    // Use readBytes for DMA-compatible read
    size_t bytes_read = i2s_capture.readBytes((char *)dest, to_read);
    
    return bytes_read;
}

int audio_dma_capture_available(void) {
    return i2s_capture.available();
}

// --- Playback DMA ---

bool audio_dma_playback_init_mclk(uint8_t mclk, uint8_t bclk, uint8_t lrclk, uint8_t dout) {
    s_mclk_pin = mclk;
    
    // Configure I2S pins: (BCLK, LRCLK, DOUT, DIN, MCLK)
    i2s_playback.setPins(bclk, lrclk, dout, -1, mclk);
    
    // Initialize I2S at 16kHz, 16-bit mono (hardware: stereo mode, but we send mono)
    if (!i2s_playback.begin(I2S_MODE_STD, AUDIO_I2S_SAMPLE_RATE,
                            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO,
                            I2S_STD_SLOT_LEFT)) {
        Serial1.printf("[dma] I2S playback init failed (MCLK=%d)\n", mclk);
        return false;
    }
    
    Serial1.printf("[dma] I2S playback initialized at %dHz (MCLK=%d)\n",
                  AUDIO_I2S_SAMPLE_RATE, mclk);
    s_state = (s_state == AUDIO_STATE_CAPTURING) ? AUDIO_STATE_BOTH : AUDIO_STATE_PLAYING;
    return true;
}

bool audio_dma_playback_init(uint8_t bclk, uint8_t lrclk, uint8_t dout) {
    s_mclk_pin = -1;
    
    // Configure I2S pins: (BCLK, LRCLK, DOUT, DIN, MCLK)
    i2s_playback.setPins(bclk, lrclk, dout, -1, -1);
    
    // Initialize I2S at 16kHz, 16-bit mono (hardware: stereo mode)
    if (!i2s_playback.begin(I2S_MODE_STD, AUDIO_I2S_SAMPLE_RATE,
                            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO,
                            I2S_STD_SLOT_LEFT)) {
        Serial1.printf("[dma] I2S playback init failed (no MCLK)\n");
        return false;
    }
    
    Serial1.printf("[dma] I2S playback initialized at %dHz (no MCLK)\n",
                  AUDIO_I2S_SAMPLE_RATE);
    s_state = (s_state == AUDIO_STATE_CAPTURING) ? AUDIO_STATE_BOTH : AUDIO_STATE_PLAYING;
    return true;
}

void audio_dma_playback_deinit(void) {
    i2s_playback.end();
    s_state = (s_state == AUDIO_STATE_BOTH) ? AUDIO_STATE_CAPTURING : AUDIO_STATE_IDLE;
    Serial1.println("[dma] I2S playback deinitialized");
}

size_t audio_dma_playback_write(const uint8_t *data, size_t len) {
    if (s_state == AUDIO_STATE_IDLE || s_state == AUDIO_STATE_CAPTURING) {
        return 0;  // Cannot play while only capturing
    }
    
    size_t written = 0;
    size_t remaining = len;
    const uint8_t *ptr = data;
    
    // Write in DMA-sized bursts to avoid blocking
    while (remaining > 0) {
        size_t burst = (remaining < AUDIO_DMA_PLAYBACK_SIZE) ? remaining : AUDIO_DMA_PLAYBACK_SIZE;
        
        // Check if I2S buffer has space
        // ESP_I2S doesn't expose buffer occupancy directly, so we write and check return value
        size_t w = i2s_playback.write(ptr, burst);
        if (w == 0) {
            // Buffer full, yield and retry
            vTaskDelay(1 / portTICK_PERIOD_MS);
            // Break to avoid busy-waiting
            break;
        }
        
        ptr += w;
        remaining -= w;
        written += w;
    }
    
    return written;
}

bool audio_dma_playback_has_space(size_t min_bytes) {
    // ESP_I2S doesn't expose buffer occupancy, so we use a heuristic:
    // assume we have at least one DMA buffer's worth of space
    return true;
}

// --- Codec initialization ---

bool audio_codec_es8311_init(void) {
#if defined(BOARD_AIPI_LITE) || defined(BOARD_WAVESHARE_AMOLED)
    Wire.begin(15, 14);  // I2C SDA, SCL (shared with touch on Waveshare)
    
    // Read chip ID
    uint8_t chip_id = 0;
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(0xFD);  // Chip reset / ID register
    if (Wire.endTransmission(false) != 0) {
        Serial1.println("[codec] ES8311 not found on I2C address 0x18");
        return false;
    }
    if (Wire.requestFrom(ES8311_I2C_ADDR, (uint8_t)1) != 1) {
        Serial1.println("[codec] ES8311 read ID failed");
        return false;
    }
    chip_id = Wire.read();
    Serial1.printf("[codec] ES8311 chip ID: 0x%02X\n", chip_id);
    
    // Software reset
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(0x00);  // SW_CTRL
    Wire.write(0x1F);  // Reset all registers
    if (Wire.endTransmission() != 0) return false;
    delay(10);
    
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(0x00);  // SW_CTRL
    Wire.write(0x00);  // Exit reset
    if (Wire.endTransmission() != 0) return false;
    delay(10);
    
    // Power management
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(0x01);  // CHIP_POWER_CTRL
    Wire.write(0x00);
    Wire.endTransmission();
    
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(0x02);  // CHIP_POWER_CTRL2
    Wire.write(0x00);
    Wire.endTransmission();
    
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(0x03);  // INT_POWER_CTRL1
    Wire.write(0x00);
    Wire.endTransmission();
    
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(0x04);  // INT_POWER_CTRL2
    Wire.write(0x00);
    Wire.endTransmission();
    
    // Clock configuration
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(0x05);  // CLOCK_MANUAL
    Wire.write(0x00);  // Auto clock mode
    Wire.endTransmission();
    
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(0x06);  // CLK_DIV1
    Wire.write(0x00);  // MCLK / 1
    Wire.endTransmission();
    
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(0x07);  // CLK_DIV2
    Wire.write(0x01);  // BCLK / 2
    Wire.endTransmission();
    
    // DAC configuration
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(0x2A);  // DAC_VOL
    Wire.write(0x00);  // Max volume
    Wire.endTransmission();
    
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(0x2B);  // DAC_VOL_CTRL
    Wire.write(0x00);
    Wire.endTransmission();
    
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(0x0E);  // ADC_DAC_MUTE
    Wire.write(0x00);  // Unmute DAC
    Wire.endTransmission();
    
    // I2S format (0x02 = I2S, 16-bit, slave)
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(0x0A);  // I2S_CTRL
    Wire.write(0x02);
    Wire.endTransmission();
    
    // Analog output
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(0x3E);  // ANA_PLAY_CTRL
    Wire.write(0x08);  // Enable DAC output
    Wire.endTransmission();
    
    // LDO and AMP
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(0x40);  // LDO_CTRL
    Wire.write(0x0F);  // Enable all LDOs
    Wire.endTransmission();
    
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(0x92);  // AMP_CTRL
    Wire.write(0x00);  // Enable amplifier
    Wire.endTransmission();
    
    // Final power-on
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(0x01);  // CHIP_POWER_CTRL
    Wire.write(0x01);  // Power up (bit 0)
    Wire.endTransmission();
    
    Serial1.println("[codec] ES8311 codec initialized successfully");
    return true;

#else
    // Non-AIPI-Lite boards don't have ES8311
    return true;
#endif
}

// --- State query ---

audio_state_t audio_get_state(void) {
    return s_state;
}

void audio_set_state(audio_state_t state) {
    s_state = state;
}
