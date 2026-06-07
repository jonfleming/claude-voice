// driver_audio_input.cpp
// I2S audio input driver for ESP32-S3 with ring buffer integration.
//
// Architecture:
//   I2S peripheral (32-bit stereo @ 16kHz)
//     -> DMA capture (ESP_I2S library)
//     -> convert_input_to_backend_pcm() (32-bit stereo -> 16-bit mono)
//     -> ring buffer (4096 bytes, 256ms depth)
//     -> recorder task drains ring buffer -> WebSocket send
//
// Latency budget:
//   - I2S DMA: 1024 bytes = 64ms
//   - Ring buffer: 4096 bytes = 256ms (worst case, full)
//   - Typical drain interval: 2ms
//   - Combined typical latency: ~50-100ms
//   - Combined worst-case latency: ~320ms
//
// The ring buffer decouples I2S DMA timing from WebSocket send timing,
// preventing buffer underruns when the network is congested.

#include "driver_audio_input.h"
#include "audio_ring_buffer.h"
#include "board_pins.h"
#include "Arduino.h"
#include "ESP_I2S.h"

// Ring buffer for captured audio (shared between I2S read and recorder task)
static audio_ring_buffer_t s_capture_rb;

// DC removal state (static to preserve across calls)
static float s_dc_offset = 0.0f;
static const float s_dc_alpha = 0.999f;

// Gain settings for Whisper accuracy
static const float s_capture_gain = 12.0f;  // ~22dB software gain

// I2S input instance
static I2SClass s_i2s_input;

// --- Public API ---

void audio_input_init(uint8_t sck, uint8_t ws, uint8_t din) {
    // Configure I2S pins: (BCLK, LRCLK, DOUT, DIN, MCLK)
    s_i2s_input.setPins(sck, ws, -1, din, -1);
    
    // Initialize I2S at 16kHz, 32-bit stereo
    if (!s_i2s_input.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_32BIT,
                           I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
        Serial1.println("[audio_in] Failed to initialize I2S bus!");
        return;
    }
    Serial1.printf("[audio_in] I2S bus initialized at 16kHz. rx=%s\r\n",
                  s_i2s_input.rxChan() ? "ok" : "null");
}

// Audio input init with MCLK pin (required for AIPI-Lite ES8311 codec)
void audio_input_init_mclk(uint8_t mclk, uint8_t sck, uint8_t ws, uint8_t din) {
    // setPins signature: (bclk, ws, dout, din, mclk)
    s_i2s_input.setPins(sck, ws, -1, din, mclk);
    
    // Initialize I2S at 16kHz, 32-bit stereo
    if (!s_i2s_input.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_32BIT,
                           I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
        Serial1.printf("[audio_in] Failed to initialize I2S bus (MCLK=%d)\n", mclk);
        return;
    }
    Serial1.printf("[audio_in] I2S bus initialized at 16kHz (MCLK=%d). rx=%s\r\n",
                  mclk, s_i2s_input.rxChan() ? "ok" : "null");
}

void audio_input_deinit(void) {
    s_i2s_input.end();
    Serial1.println("[audio_in] I2S deinitialized");
}

uint8_t* audio_input_record_wav(uint32_t duration, size_t& wav_size) {
    return s_i2s_input.recordWAV(duration, &wav_size);
}

void audio_input_print_buffer(uint8_t* buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        Serial1.print(buffer[i]);
        Serial1.print(" ");
    }
    Serial1.println();
}

size_t audio_input_read_iis_data(char* buffer, size_t size) {
    return s_i2s_input.readBytes(buffer, size);
}

int audio_input_get_iis_data_available(void) {
    return s_i2s_input.available();
}

// --- Ring buffer integration ---

void audio_input_ring_buffer_init(void) {
    audio_ring_buffer_init(&s_capture_rb);
    s_dc_offset = 0.0f;
}

audio_ring_buffer_t* audio_input_ring_buffer_get(void) {
    return &s_capture_rb;
}

size_t audio_input_ring_buffer_drain(int16_t *dst, size_t max_samples) {
    return audio_ring_buffer_read(&s_capture_rb, dst, max_samples);
}

size_t audio_input_ring_buffer_available_samples(void) {
    return audio_ring_buffer_available(&s_capture_rb);
}

// --- I2S DMA capture with ring buffer fill ---

// Read from I2S DMA, convert to 16-bit mono, and fill ring buffer.
// This is called by the recorder task in a polling loop.
//
// Returns number of bytes read from I2S (not ring buffer space filled).
size_t audio_input_dma_capture_drain(int16_t *mono_dst, size_t mono_capacity) {
    // 1. Read raw I2S data
    int iis_available = s_i2s_input.available();
    if (iis_available <= 0) return 0;
    
    // 2. Read into temporary buffer
    static uint8_t iis_buf[AUDIO_DMA_CAPTURE_SIZE];  // 1024 bytes
    size_t bytes_read = (size_t)s_i2s_input.readBytes((char *)iis_buf, sizeof(iis_buf));
    if (bytes_read == 0) return 0;
    
    // 3. Convert 32-bit stereo to 16-bit mono
    static int16_t mono_buf[512];  // 1024 bytes / 2 = 512 mono samples
    uint32_t mono_count = audio_ring_buffer_convert_stereo_to_mono(
        iis_buf, bytes_read,
        mono_buf, 512,
        s_capture_gain, s_dc_alpha
    );
    
    // 4. Fill ring buffer (may drop samples if full)
    uint32_t ring_written = audio_ring_buffer_write(&s_capture_rb, mono_buf, mono_count);
    
    // 5. Also pass data to caller's buffer (for direct WebSocket send)
    if (mono_dst && mono_capacity > 0 && ring_written > 0) {
        size_t to_copy = (ring_written < mono_capacity) ? ring_written : mono_capacity;
        memcpy(mono_dst, mono_buf, to_copy * sizeof(int16_t));
    }
    
    return bytes_read;
}

// Backward-compatible wrapper for the recorder task
size_t audio_input_capture_to_ring(char *buffer, size_t size) {
    // This is called by the recorder task to get raw I2S data.
    // For backward compatibility, we return raw I2S data.
    // The ring buffer path is used via audio_input_dma_capture_drain() instead.
    return s_i2s_input.readBytes(buffer, size);
}
