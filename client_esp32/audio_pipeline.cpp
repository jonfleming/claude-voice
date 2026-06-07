// audio_pipeline.cpp
// Unified audio pipeline implementation for ESP32-S3 voice assistant.
//
// This module ties together:
//   - driver_audio_input (I2S capture, ring buffer)
//   - driver_audio_output (I2S playback, ring buffer)
//   - audio_ring_buffer (thread-safe ring buffer)
//   - audio_dma_buffer (DMA buffer management)
//
// It provides a single entry point for initializing and managing the
// entire audio subsystem, with health monitoring and error reporting.

#include "audio_pipeline.h"
#include "driver_audio_input.h"
#include "driver_audio_output.h"
#include "audio_ring_buffer.h"
#include "board_pins.h"
#include "Arduino.h"
#include <Wire.h>

// =============================================================================
// Extern references to driver_audio_output.cpp globals
// =============================================================================
extern I2SClass i2s_output;
extern uint16_t s_bits_per_sample;
extern uint16_t s_channels;

// =============================================================================
// Internal state
// =============================================================================

static pipeline_state_t s_pipeline_state = PIPELINE_IDLE;
static pipeline_health_t s_health = {0, 0, 0, 0, PIPELINE_ERR_NONE};

// =============================================================================
// Initialization
// =============================================================================

bool audio_pipeline_init(const char *board_profile) {
    Serial1.println("[pipeline] Initializing audio pipeline...");
    
    // Initialize capture ring buffer
    audio_input_ring_buffer_init();
    Serial1.println("[pipeline] Capture ring buffer initialized");
    
    // Initialize playback ring buffer
    audio_output_ring_buffer_init();
    Serial1.println("[pipeline] Playback ring buffer initialized");
    
    // Initialize I2S capture
#ifdef BOARD_AIPI_LITE
    audio_input_init_mclk(AUDIO_INPUT_MCLK, AUDIO_INPUT_BCLK, AUDIO_INPUT_LRCLK, AUDIO_INPUT_DIN);
#elif defined(BOARD_WAVESHARE_AMOLED)
    // Waveshare uses the same ES8311 codec as AIPI-Lite
    audio_input_init_mclk(AUDIO_I2S_MCK, AUDIO_I2S_BCK, AUDIO_I2S_WS, AUDIO_I2S_DI);
#else
    audio_input_init(AUDIO_INPUT_SCK, AUDIO_INPUT_WS, AUDIO_INPUT_DIN);
#endif
    
    // Initialize I2S playback
#ifdef BOARD_AIPI_LITE
    if (!audio_output_codec_init()) {
        Serial1.println("[pipeline] ERROR: ES8311 codec init failed");
        s_health.last_error_code = PIPELINE_ERR_CODEC_INIT;
        return false;
    }
    i2s_output_init_mclk(AUDIO_OUTPUT_MCLK, AUDIO_OUTPUT_BCLK, AUDIO_OUTPUT_LRC, AUDIO_OUTPUT_DOUT);
#elif defined(BOARD_WAVESHARE_AMOLED)
    // Waveshare ES8311 init: codec + I2S output at 16kHz/16-bit
    // ES8311 I2C address is 0x18 on Waveshare board
    // I2C bus shared with touch controller: SDA=GPIO15, SCL=GPIO14
    Wire.begin(15, 14);
    if (!audio_output_codec_init()) {
        Serial1.println("[pipeline] ERROR: ES8311 codec init failed");
        s_health.last_error_code = PIPELINE_ERR_CODEC_INIT;
        return false;
    }
    // Enable power amplifier (GPIO46, active HIGH)
    pinMode(AUDIO_PA_PIN, OUTPUT);
    digitalWrite(AUDIO_PA_PIN, HIGH);
    // Initialize I2S at 16kHz, 16-bit, stereo (ES8311 standard)
    i2s_output.setPins(AUDIO_I2S_BCK, AUDIO_I2S_WS, AUDIO_I2S_DO, -1, AUDIO_I2S_MCK);
    if (!i2s_output.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT,
                           I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_LEFT)) {
        Serial1.println("[pipeline] ERROR: I2S playback init failed");
        s_health.last_error_code = PIPELINE_ERR_I2S_INIT;
        return false;
    }
    s_bits_per_sample = 16;
    s_channels = 2;
#else
    i2s_output_init(AUDIO_OUTPUT_BCLK, AUDIO_OUTPUT_LRC, AUDIO_OUTPUT_DOUT);
#endif
    
    // Set default volume
    audio_output_set_volume(10);  // ~half volume
    
    s_pipeline_state = PIPELINE_IDLE;
    Serial1.println("[pipeline] Audio pipeline initialized successfully");
    return true;
}

// =============================================================================
// Capture API
// =============================================================================

bool audio_pipeline_capture_start(void) {
#ifdef BOARD_AIPI_LITE
    // Re-initialize I2S capture (ES8311 may have been reconfigured for playback)
    audio_input_init_mclk(AUDIO_INPUT_MCLK, AUDIO_INPUT_BCLK, AUDIO_INPUT_LRCLK, AUDIO_INPUT_DIN);
#elif defined(BOARD_WAVESHARE_AMOLED)
    audio_input_init_mclk(AUDIO_I2S_MCK, AUDIO_I2S_BCK, AUDIO_I2S_WS, AUDIO_I2S_DI);
#endif
    
    s_pipeline_state = (s_pipeline_state == PIPELINE_PLAYING) ? PIPELINE_ACTIVE : PIPELINE_CAPTURING;
    Serial1.println("[pipeline] Capture started");
    return true;
}

void audio_pipeline_capture_stop(void) {
    audio_input_deinit();
    s_pipeline_state = (s_pipeline_state == PIPELINE_ACTIVE) ? PIPELINE_PLAYING : PIPELINE_IDLE;
    Serial1.println("[pipeline] Capture stopped");
}

size_t audio_pipeline_capture_drain(int16_t *mono_output, size_t max_samples, size_t *bytes_written) {
    // Check ring buffer first (fast path - no I2S read needed)
    size_t available = audio_input_ring_buffer_available_samples();
    if (available == 0) {
        // Try reading from I2S DMA directly
        size_t iis_bytes = audio_input_get_iis_data_available();
        if (iis_bytes <= 0) {
            if (bytes_written) *bytes_written = 0;
            return 0;
        }
        
        // Read from I2S, convert, and fill ring buffer
        size_t i2s_read = audio_input_dma_capture_drain(mono_output, max_samples);
        if (bytes_written) *bytes_written = i2s_read;
        return i2s_read / 2;  // Return mono sample count
    }
    
    // Drain from ring buffer (fast path)
    size_t drain_count = audio_input_ring_buffer_drain(mono_output, max_samples);
    if (bytes_written) *bytes_written = drain_count * sizeof(int16_t);
    
    // Also try to refill ring buffer from I2S
    size_t iis_bytes = audio_input_get_iis_data_available();
    if (iis_bytes > 0) {
        static int16_t tmp_buf[256];
        audio_input_dma_capture_drain(tmp_buf, 256);
    }
    
    return drain_count;
}

size_t audio_pipeline_capture_available(void) {
    return audio_input_ring_buffer_available_samples();
}

// =============================================================================
// Playback API
// =============================================================================

bool audio_pipeline_playback_start(void) {
#ifdef BOARD_AIPI_LITE
    // Enable speaker amp
    digitalWrite(SPEAKER_AMP_ENABLE, HIGH);
    delay(10);
    
    // Reconfigure I2S for playback
    if (!i2s_output_init_mclk(AUDIO_OUTPUT_MCLK, AUDIO_OUTPUT_BCLK, AUDIO_OUTPUT_LRC, AUDIO_OUTPUT_DOUT)) {
        Serial1.println("[pipeline] ERROR: I2S playback init failed");
        s_health.last_error_code = PIPELINE_ERR_I2S_INIT;
        return false;
    }
#elif defined(BOARD_WAVESHARE_AMOLED)
    // Enable power amplifier
    pinMode(AUDIO_PA_PIN, OUTPUT);
    digitalWrite(AUDIO_PA_PIN, HIGH);
    
    i2s_output_init(AUDIO_OUTPUT_BCLK, AUDIO_OUTPUT_LRC, AUDIO_OUTPUT_DOUT);
#else
    i2s_output_init(AUDIO_OUTPUT_BCLK, AUDIO_OUTPUT_LRC, AUDIO_OUTPUT_DOUT);
#endif
    
    s_pipeline_state = (s_pipeline_state == PIPELINE_CAPTURING) ? PIPELINE_ACTIVE : PIPELINE_PLAYING;
    Serial1.println("[pipeline] Playback started");
    return true;
}

void audio_pipeline_playback_stop(void) {
#ifdef BOARD_AIPI_LITE
    digitalWrite(SPEAKER_AMP_ENABLE, LOW);
    // Re-initialize capture (ES8311 RX path may have been released)
    audio_input_init_mclk(AUDIO_INPUT_MCLK, AUDIO_INPUT_BCLK, AUDIO_INPUT_LRCLK, AUDIO_INPUT_DIN);
#elif defined(BOARD_WAVESHARE_AMOLED)
    digitalWrite(AUDIO_PA_PIN, LOW);
#endif
    
    i2s_output_stream_end();
    s_pipeline_state = (s_pipeline_state == PIPELINE_ACTIVE) ? PIPELINE_CAPTURING : PIPELINE_IDLE;
    Serial1.println("[pipeline] Playback stopped");
}

size_t audio_pipeline_playback_queue(const int16_t *src, size_t count) {
    size_t queued = audio_output_ring_buffer_write(src, count);
    if (queued < count) {
        s_health.samples_dropped_playback += (count - queued);
    }
    return queued;
}

size_t audio_pipeline_playback_drain_to_i2s(void) {
    size_t available = audio_output_ring_buffer_available_samples();
    if (available == 0) return 0;
    
    // Read from ring buffer in chunks
    static int16_t playback_buf[256];  // 512 bytes per chunk
    size_t to_read = (available < 256) ? available : 256;
    size_t drained = audio_output_ring_buffer_drain(playback_buf, to_read);
    
    if (drained == 0) return 0;
    
    // Mono-to-stereo expansion and I2S write
    size_t bytes_written = 0;
    
    // Check if we're in 16-bit mono mode
    if (s_bits_per_sample == 16 && s_channels == 1) {
        // Expand mono to stereo for I2S output
        static int16_t stereo_buf[256 * 2];
        for (size_t i = 0; i < drained; i++) {
            stereo_buf[i * 2] = playback_buf[i];
            stereo_buf[i * 2 + 1] = playback_buf[i];
        }
        bytes_written = i2s_output.write((uint8_t*)stereo_buf, drained * 4);
    } else {
        // Already stereo or 32-bit
        bytes_written = i2s_output.write((uint8_t*)playback_buf, drained * 2);
    }
    
    if (bytes_written == 0) {
        s_health.i2s_underrun_count++;
    }
    
    return bytes_written;
}

size_t audio_pipeline_playback_available(void) {
    return audio_output_ring_buffer_available_samples();
}

// =============================================================================
// Health monitoring
// =============================================================================

pipeline_health_t audio_pipeline_get_health(void) {
    return s_health;
}

void audio_pipeline_reset_health(void) {
    s_health = {0, 0, 0, 0, PIPELINE_ERR_NONE};
}

pipeline_state_t audio_pipeline_get_state(void) {
    return s_pipeline_state;
}
