// audio_dma_buffer.h
// I2S DMA buffer management for ESP32-S3.
// Wraps the ESP_I2S Arduino library to provide double-buffered DMA
// for continuous audio capture and playback with minimal latency.
//
// Design:
//   - Capture: Two 1024-byte DMA buffers (ping-pong). ESP_I2S library
//     handles the DMA descriptor chain internally. The recorder task
//     reads from the I2S peripheral via readBytes().
//   - Playback: Streaming write to I2S peripheral. Buffer underrun
//     detection via available() polling.
//
// DMA buffer sizes:
//   - I2S capture: 1024 bytes (64ms @ 16kHz/32-bit stereo)
//   - I2S playback: 512 bytes per write burst (32ms @ 16kHz/16-bit mono)
//   - Ring buffer: 4096 bytes (256ms @ 16kHz/16-bit mono)
//
// Combined worst-case latency: ~320ms
// Typical latency: ~50-100ms (ring buffer drains every 2ms)

#ifndef AUDIO_DMA_BUFFER_H
#define AUDIO_DMA_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include "Arduino.h"
#include "ESP_I2S.h"

// DMA buffer sizes
#define AUDIO_DMA_CAPTURE_SIZE    1024  // bytes per DMA read
#define AUDIO_DMA_PLAYBACK_SIZE   512   // bytes per DMA write burst
#define AUDIO_DMA_NUM_BUFFERS     2     // ping-pong double buffer

// I2S sample format constants
#define AUDIO_I2S_SAMPLE_RATE     16000
#define AUDIO_I2S_BITS_PER_SAMPLE I2S_DATA_BIT_WIDTH_32BIT
#define AUDIO_I2S_CHANNEL_MODE    I2S_SLOT_MODE_STEREO
#define AUDIO_I2S_DATA_FORMAT     I2S_STD_SLOT_BOTH

// Audio pipeline state
typedef enum {
    AUDIO_STATE_IDLE,
    AUDIO_STATE_CAPTURING,
    AUDIO_STATE_PLAYING,
    AUDIO_STATE_BOTH,
    AUDIO_STATE_ERROR
} audio_state_t;

// DMA buffer descriptor for capture (double buffer)
typedef struct {
    uint8_t *buffers[AUDIO_DMA_NUM_BUFFERS];  // allocated DMA-capable buffers
    volatile uint8_t *active;                   // currently active buffer
    volatile uint8_t *next;                     // next buffer to read
    volatile uint32_t buffer_size;
    volatile uint32_t active_idx;
} audio_capture_dma_t;

// DMA buffer descriptor for playback
typedef struct {
    uint8_t *buffer;      // allocated DMA-capable buffer
    volatile size_t filled; // bytes filled in buffer
    volatile size_t size;
} audio_playback_dma_t;

// --- Capture (I2S input) ---

// Initialize I2S for capture with MCLK (ES8311 codec boards)
// Returns true on success.
bool audio_dma_capture_init_mclk(uint8_t mclk, uint8_t bclk, uint8_t lrclk, uint8_t din);

// Initialize I2S for capture without MCLK (Freenove external mic)
// Returns true on success.
bool audio_dma_capture_init(uint8_t bclk, uint8_t lrclk, uint8_t din);

// Deinitialize capture I2S
void audio_dma_capture_deinit(void);

// Read available I2S data into the ring buffer.
// Returns number of bytes read from I2S (may be 0 if nothing available).
size_t audio_dma_capture_read(uint8_t *dest, size_t max_bytes);

// Get number of bytes available for reading from I2S
int audio_dma_capture_available(void);

// --- Playback (I2S output) ---

// Initialize I2S for playback with MCLK (ES8311 codec boards)
// Returns true on success.
bool audio_dma_playback_init_mclk(uint8_t mclk, uint8_t bclk, uint8_t lrclk, uint8_t dout);

// Initialize I2S for playback without MCLK (Freenove)
// Returns true on success.
bool audio_dma_playback_init(uint8_t bclk, uint8_t lrclk, uint8_t dout);

// Deinitialize playback I2S
void audio_dma_playback_deinit(void);

// Write PCM data to I2S playback buffer.
// Returns number of bytes actually written (may be < len if buffer full).
size_t audio_dma_playback_write(const uint8_t *data, size_t len);

// Check if playback buffer has space
bool audio_dma_playback_has_space(size_t min_bytes);

// --- Codec initialization ---

// Initialize the ES8311 codec (AIPI-Lite and Waveshare boards)
// Returns true on success.
bool audio_codec_es8311_init(void);

// --- State query ---

// Get current audio pipeline state
audio_state_t audio_get_state(void);

// Set audio pipeline state
void audio_set_state(audio_state_t state);

#endif // AUDIO_DMA_BUFFER_H
