#ifndef __DRIVER_AUDIO_INPUT_H
#define __DRIVER_AUDIO_INPUT_H

#include "Arduino.h"
#include "ESP_I2S.h"
#include "audio_ring_buffer.h"

// I2S DMA buffer size (1024 bytes = 64ms @ 16kHz/32-bit stereo)
#define AUDIO_DMA_CAPTURE_SIZE 1024

// Ring buffer drain capacity per call (512 mono samples = 1024 bytes)
#define AUDIO_RING_DRAIN_MAX  512

// Public API

// Initialize I2S for capture (no MCLK - Freenove external mic)
void audio_input_init(uint8_t sck, uint8_t ws, uint8_t din);

// Initialize I2S for capture with MCLK (AIPI-Lite / Waveshare ES8311 codec)
void audio_input_init_mclk(uint8_t mclk, uint8_t sck, uint8_t ws, uint8_t din);

// Deinitialize capture I2S
void audio_input_deinit(void);

// Record WAV file (legacy API, uses ESP_I2S built-in)
uint8_t* audio_input_record_wav(uint32_t duration, size_t& wav_size);

// Print buffer contents to serial (debug)
void audio_input_print_buffer(uint8_t* buffer, size_t size);

// Read raw I2S data (legacy API)
size_t audio_input_read_iis_data(char* buffer, size_t size);

// Get available I2S data size (legacy API)
int audio_input_get_iis_data_available(void);

// --- Ring buffer API ---

// Initialize the capture ring buffer (call once during setup)
void audio_input_ring_buffer_init(void);

// Get ring buffer reference (for use by recorder task)
audio_ring_buffer_t* audio_input_ring_buffer_get(void);

// Drain ring buffer into output buffer.
// Returns number of int16_t samples drained.
size_t audio_input_ring_buffer_drain(int16_t *dst, size_t max_samples);

// Check ring buffer availability (in int16_t samples)
size_t audio_input_ring_buffer_available_samples(void);

// DMA capture drain: read from I2S, convert to mono, fill ring buffer.
// Returns bytes read from I2S.
size_t audio_input_dma_capture_drain(int16_t *mono_dst, size_t mono_capacity);

#endif
