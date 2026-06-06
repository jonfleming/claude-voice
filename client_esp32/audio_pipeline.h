// audio_pipeline.h
// Unified audio pipeline API for ESP32-S3 voice assistant.
//
// This module provides a single entry point for:
//   1. Initializing the entire audio subsystem (I2S, codec, ring buffers)
//   2. Starting/stopping continuous capture with ring buffer
//   3. Starting/stopping playback from ring buffer
//   4. Monitoring pipeline health (underrun/overrun detection)
//
// Architecture overview:
//
//   Capture path:
//     I2S peripheral (32-bit stereo @ 16kHz)
//       -> DMA read (ESP_I2S library)
//       -> convert_stereo_to_mono() (32-bit stereo -> 16-bit mono)
//       -> ring buffer write (4096 bytes)
//       -> recorder task reads ring buffer -> WebSocket send
//
//   Playback path:
//     WebSocket receives audio -> ring buffer write (4096 bytes)
//       -> player task reads ring buffer -> mono-to-stereo expansion
//       -> I2S streaming write -> ES8311 DAC / I2S peripheral
//
// Latency budget:
//   - Capture: I2S DMA (64ms) + ring buffer drain (2ms) = ~66ms typical
//   - Playback: ring buffer (256ms worst, ~10ms typical) + I2S (1ms) = ~11ms typical
//   - End-to-end: ~50-100ms typical, <320ms worst case
//
// Acceptance criteria:
//   - <50ms end-to-end latency (typical)
//   - No buffer underruns/overruns during continuous operation

#ifndef AUDIO_PIPELINE_H
#define AUDIO_PIPELINE_H

#include <stdint.h>
#include <stddef.h>
#include "Arduino.h"

// =============================================================================
// Board selection (must match sketch_config.h)
// =============================================================================
// BOARD_AIPI_LITE - AIPI-Lite board with ES8311 codec
// BOARD_WAVESHARE_AMOLED - Waveshare ESP32-S3-Touch-AMOLED-1.8
// (default) Freenove - Freenove ESP32-S3 Media Kit

// =============================================================================
// Audio pipeline state
// =============================================================================
typedef enum {
    PIPELINE_IDLE,           // No audio active
    PIPELINE_CAPTURING,      // Capture active, playback idle
    PIPELINE_PLAYING,        // Playback active, capture idle
    PIPELINE_ACTIVE,         // Both capture and playback active
    PIPELINE_ERROR           // Error state (hardware failure)
} pipeline_state_t;

// =============================================================================
// Pipeline health metrics
// =============================================================================
typedef struct {
    uint32_t samples_dropped_capture;  // Samples dropped in capture ring buffer (full)
    uint32_t samples_dropped_playback; // Samples dropped in playback ring buffer (full)
    uint32_t i2s_underrun_count;       // I2S playback buffer underruns
    uint32_t i2s_overrun_count;        // I2S capture buffer overruns
    uint32_t last_error_code;          // Last error code (0 = no error)
} pipeline_health_t;

// =============================================================================
// Initialization
// =============================================================================

// Initialize the entire audio pipeline.
// Must be called during setup() before any audio operations.
//
// Parameters:
//   board_profile - "AIPI_LITE", "WAVESHARE", or "FREENOVE"
//
// Returns true on success.
bool audio_pipeline_init(const char *board_profile);

// =============================================================================
// Capture API (recorder task)
// =============================================================================

// Start continuous audio capture.
// Initializes I2S input, ES8311 codec (if applicable), and capture ring buffer.
// Returns true on success.
bool audio_pipeline_capture_start(void);

// Stop continuous audio capture.
// Deinitializes I2S input.
void audio_pipeline_capture_stop(void);

// Drain captured audio from the ring buffer.
// This is called by the recorder task in a polling loop.
//
// Parameters:
//   mono_output   - buffer for 16-bit mono samples
//   max_samples   - maximum samples to drain
//   bytes_written - [out] number of bytes actually drained
//
// Returns number of bytes read from I2S (may be 0 if nothing available).
size_t audio_pipeline_capture_drain(int16_t *mono_output, size_t max_samples, size_t *bytes_written);

// Get number of samples available in capture ring buffer
size_t audio_pipeline_capture_available(void);

// =============================================================================
// Playback API (player task)
// =============================================================================

// Start audio playback from ring buffer.
// Initializes I2S output, ES8311 codec (if applicable), and playback ring buffer.
// Returns true on success.
bool audio_pipeline_playback_start(void);

// Stop audio playback.
// Deinitializes I2S output.
void audio_pipeline_playback_stop(void);

// Queue audio samples to the playback ring buffer.
// This is called by the player task or WebSocket handler.
//
// Parameters:
//   src     - 16-bit mono samples to queue
//   count   - number of samples
//
// Returns number of samples actually queued (may be < count if full).
size_t audio_pipeline_playback_queue(const int16_t *src, size_t count);

// Drain playback ring buffer to I2S.
// This is called by the player task in a polling loop.
// Handles mono-to-stereo expansion automatically.
//
// Returns number of bytes written to I2S.
size_t audio_pipeline_playback_drain_to_i2s(void);

// Get number of samples available in playback ring buffer
size_t audio_pipeline_playback_available(void);

// =============================================================================
// Health monitoring
// =============================================================================

// Get current pipeline health metrics
pipeline_health_t audio_pipeline_get_health(void);

// Reset health metrics
void audio_pipeline_reset_health(void);

// Get current pipeline state
pipeline_state_t audio_pipeline_get_state(void);

// =============================================================================
// Error codes
// =============================================================================
#define PIPELINE_ERR_NONE          0
#define PIPELINE_ERR_I2S_INIT     1
#define PIPELINE_ERR_CODEC_INIT   2
#define PIPELINE_ERR_ES8311_NOT_FOUND  3
#define PIPELINE_ERR_PSRAM_ALLOC  4
#define PIPELINE_ERR_DMA_FULL     5

#endif // AUDIO_PIPELINE_H
