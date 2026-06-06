#ifndef __DRIVER_AUDIO_OUTPUT_H
#define __DRIVER_AUDIO_OUTPUT_H

#include "Arduino.h"
#include "stdint.h"
#include "audio_ring_buffer.h"

// Playback ring buffer capacity (4096 bytes = 256ms of 16-bit mono audio)
#define AUDIO_PLAYBACK_RING_CAPACITY 4096

bool i2s_output_init(int bclk, int lrc, int dout);
bool i2s_output_init_mclk(int mclk, int bclk, int lrc, int dout);
bool audio_output_codec_init(void);
void i2s_output_wav(uint8_t *data, size_t len);
void i2s_output_deinit(void);

// Streaming API: begin a streaming session (configure I2S to WAV params),
// write PCM bytes incrementally, and end the streaming session.
bool i2s_output_stream_begin(uint32_t sample_rate, uint16_t bits_per_sample, uint16_t channels);
size_t i2s_output_stream_write(const uint8_t *data, size_t len);
void i2s_output_stream_end(void);

// --- Ring buffer playback API ---

// Initialize playback ring buffer (call once during setup)
void audio_output_ring_buffer_init(void);

// Get playback ring buffer reference (for use by player task)
audio_ring_buffer_t* audio_output_ring_buffer_get(void);

// Write audio to playback ring buffer.
// Returns number of int16_t samples actually queued (may be < count if full).
size_t audio_output_ring_buffer_write(const int16_t *src, size_t count);

// Check playback ring buffer availability (in int16_t samples)
size_t audio_output_ring_buffer_available_samples(void);

// Check playback ring buffer free space (in int16_t samples)
size_t audio_output_ring_buffer_free_samples(void);

// --- Legacy API (backward compatible) ---

int audio_output_init(int bclk, int lrc, int dout);
void audio_output_set_volume(int volume);
int audio_read_output_volume(void);
void audio_output_load_music(const char *name);
void audio_output_pause_resume(void);
void audio_output_stop(void);
bool audio_output_is_running(void);
long audio_get_total_output_playing_time(void);
long audio_output_get_file_duration(void);
bool audio_output_set_play_position(int second);
long audio_read_output_play_position(void);
void audio_output_loop(void);
void audio_info(const char *info);
void audio_eof_mp3(const char *info);

#endif
