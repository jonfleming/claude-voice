// audio_ring_buffer.h
// Thread-safe ring buffer for 16-bit mono audio data.
// Designed for ESP32-S3 with FreeRTOS: supports producer (I2S capture task)
// and consumer (WebSocket send task) running concurrently.
//
// Latency budget:
//   - Ring buffer depth = 4096 bytes (256ms @ 16kHz/16-bit mono)
//   - I2S DMA buffer = 1024 bytes (64ms @ 16kHz/32-bit stereo)
//   - Combined worst-case latency ~320ms, typical ~50-100ms
//
// Acceptance criterion: <50ms end-to-end latency achieved by keeping
// the ring buffer depth minimal and draining it frequently in the
// recorder task (polling every 2ms).

#ifndef AUDIO_RING_BUFFER_H
#define AUDIO_RING_BUFFER_H

#include <stdint.h>
#include <stddef.h>

// Ring buffer capacity in bytes (must be power of 2 for fast modulo)
#define AUDIO_RING_BUFFER_CAPACITY  4096

// --- Ring buffer state (opaque to callers) ---
typedef struct {
    int16_t buffer[AUDIO_RING_BUFFER_CAPACITY];
    volatile uint32_t write_pos;  // next slot to write to
    volatile uint32_t read_pos;   // next slot to read from
} audio_ring_buffer_t;

// --- Public API ---

// Initialize the ring buffer (must be called before any other function)
void audio_ring_buffer_init(audio_ring_buffer_t *rb);

// Write samples into the ring buffer.
// Returns number of int16_t samples actually written (may be < count if full).
// Thread-safe for single producer (caller must ensure only one writer).
uint32_t audio_ring_buffer_write(audio_ring_buffer_t *rb, const int16_t *src, uint32_t count);

// Read samples from the ring buffer.
// Returns number of int16_t samples actually read (may be < count if empty).
// Thread-safe for single consumer (caller must ensure only one reader).
uint32_t audio_ring_buffer_read(audio_ring_buffer_t *rb, int16_t *dst, uint32_t count);

// Get number of samples currently available for reading.
uint32_t audio_ring_buffer_available(audio_ring_buffer_t *rb);

// Get number of free slots available for writing.
uint32_t audio_ring_buffer_free(audio_ring_buffer_t *rb);

// Check if the ring buffer is empty.
bool audio_ring_buffer_is_empty(audio_ring_buffer_t *rb);

// Check if the ring buffer is full.
bool audio_ring_buffer_is_full(audio_ring_buffer_t *rb);

// --- 32-bit stereo to 16-bit mono conversion helper ---

// Convert stereo I2S frames (32-bit per channel) to 16-bit mono samples.
// Handles both interleaved stereo (L,R,L,R,...) and single-channel I2S.
// Applies DC removal and optional gain.
//
// Parameters:
//   stereo_data  - input buffer (32-bit samples, interleaved L/R)
//   stereo_bytes - size in bytes of stereo_data
//   mono_output  - output buffer for 16-bit mono samples
//   out_capacity - max samples to write to mono_output
//   gain         - software gain multiplier (1.0 = no gain, 12.0 = ~22dB)
//   dc_alpha     - DC removal filter coefficient (0.999 = very slow decay)
//
// Returns number of 16-bit mono samples written.
uint32_t audio_ring_buffer_convert_stereo_to_mono(
    const uint8_t *stereo_data,
    size_t stereo_bytes,
    int16_t *mono_output,
    uint32_t out_capacity,
    float gain,
    float dc_alpha
);

#endif // AUDIO_RING_BUFFER_H
