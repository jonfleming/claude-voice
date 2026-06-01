
#include "driver_audio_output.h"
#include "Audio.h"
#include <ESP_I2S.h>

Audio audio;
I2SClass i2s_output; 

static uint16_t s_bits_per_sample = 32;
static uint16_t s_channels = 2;
static float s_volume_factor = 0.476f; // Default to ~10/21

extern int SPEAKER_AMP_ENABLE;  // GPIO9, defined in client_esp32.ino

bool i2s_output_init(int bclk, int lrc, int dout) {
  i2s_output.setPins(bclk, lrc, dout);
  // Default to 32kHz (can be adjusted by WAV header). Note: AIPI-Lite uses 16kHz
  // but the codec auto-resamples, so 32kHz works for initialization
  if (!i2s_output.begin(I2S_MODE_STD, 32000, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
    Serial.println("Failed to initialize I2S output bus!");
    return false;
  }
  s_bits_per_sample = 32;
  s_channels = 2;
  return true;
}

extern volatile TaskHandle_t player_task_handle;

void i2s_output_wav(uint8_t *data, size_t len)
{
  size_t data_offset = 0;
  size_t data_size = 0;
  uint32_t sample_rate = 32000;
  uint16_t channels = 2;
  uint16_t bits_per_sample = 32;

  // Inspect WAV header (if present) and reconfigure I2S
  if (len >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' && data[8] == 'W' && data[9] == 'A' && data[10] == 'V' && data[11] == 'E') {
    // Parse chunks starting at offset 12
    size_t offset = 12;
    while (offset + 8 <= len) {
      const char *cid = (const char *)(data + offset);
      uint32_t csize = (uint32_t)data[offset+4] | ((uint32_t)data[offset+5] << 8) | ((uint32_t)data[offset+6] << 16) | ((uint32_t)data[offset+7] << 24);
      size_t chunk_data_offset = offset + 8;
      if (chunk_data_offset + csize > len) break;

      if (cid[0]=='f' && cid[1]=='m' && cid[2]=='t' && cid[3]==' ') {
        if (csize >= 16) {
          channels = (uint16_t)data[chunk_data_offset+2] | ((uint16_t)data[chunk_data_offset+3] << 8);
          sample_rate = (uint32_t)data[chunk_data_offset+4] | ((uint32_t)data[chunk_data_offset+5] << 8) | ((uint32_t)data[chunk_data_offset+6] << 16) | ((uint32_t)data[chunk_data_offset+7] << 24);
          bits_per_sample = (uint16_t)data[chunk_data_offset+14] | ((uint16_t)data[chunk_data_offset+15] << 8);
        }
      } else if (cid[0]=='d' && cid[1]=='a' && cid[2]=='t' && cid[3]=='a') {
        data_offset = chunk_data_offset;
        data_size = csize;
      }
      offset = chunk_data_offset + csize;
      if (csize & 1) offset++;
    }

    Serial.printf("WAV: rate=%u, ch=%u, bits=%u, off=%u, sz=%u\r\n", sample_rate, channels, bits_per_sample, (unsigned)data_offset, (unsigned)data_size);

    s_bits_per_sample = bits_per_sample;
    s_channels = channels;

    i2s_data_bit_width_t data_bit_width = (bits_per_sample <= 16) ? I2S_DATA_BIT_WIDTH_16BIT : I2S_DATA_BIT_WIDTH_32BIT;
    
    // Always use STEREO mode for hardware compatibility, expansion handled in loop
    i2s_output.end();
    if (!i2s_output.begin(I2S_MODE_STD, sample_rate, data_bit_width, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
      Serial.println("I2S begin failed, fallback to 32k/32b/Stereo");
      i2s_output.begin(I2S_MODE_STD, 32000, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH);
      s_bits_per_sample = 32;
      s_channels = 2;
    }
  }

  const uint8_t *pcm_start = (data_offset > 0) ? (data + data_offset) : (data + 44);
  size_t pcm_len = (data_size > 0) ? data_size : (len > 44 ? len - 44 : 0);

  // Scale volume in-place
  if (s_volume_factor < 0.99f) {
    if (s_bits_per_sample == 16) {
      int16_t *s = (int16_t *)pcm_start;
      for (size_t i = 0; i < pcm_len / 2; i++) s[i] = (int16_t)(s[i] * s_volume_factor);
    } else if (s_bits_per_sample == 32) {
      int32_t *s = (int32_t *)pcm_start;
      for (size_t i = 0; i < pcm_len / 4; i++) s[i] = (int32_t)(s[i] * s_volume_factor);
    }
  }

  // Playback loop with Mono-to-Stereo expansion
  size_t bytes_left = pcm_len;
  const uint8_t *curr = pcm_start;
  while (bytes_left > 0 && player_task_handle != NULL) {
    if (s_channels == 1 && s_bits_per_sample == 16) {
      int16_t stereo_buf[256 * 2];
      size_t samples = (bytes_left / 2 < 256) ? bytes_left / 2 : 256;
      for (size_t i = 0; i < samples; i++) {
        int16_t s = ((int16_t*)curr)[i];
        stereo_buf[i*2] = s; stereo_buf[i*2+1] = s;
      }
      i2s_output.write((uint8_t*)stereo_buf, samples * 4);
      curr += samples * 2;
      bytes_left -= samples * 2;
    } else {
      size_t to_write = (bytes_left < 512) ? bytes_left : 512;
      i2s_output.write((uint8_t *)curr, to_write);
      curr += to_write;
      bytes_left -= to_write;
    }
    vTaskDelay(1);
  }
}

bool i2s_output_stream_begin(uint32_t sample_rate, uint16_t bits_per_sample, uint16_t channels) {
  i2s_output.end();
  s_bits_per_sample = bits_per_sample;
  s_channels = channels;
  i2s_data_bit_width_t data_bit_width = (bits_per_sample <= 16) ? I2S_DATA_BIT_WIDTH_16BIT : I2S_DATA_BIT_WIDTH_32BIT;
  // Always use STEREO hardware mode
  return i2s_output.begin(I2S_MODE_STD, sample_rate, data_bit_width, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH);
}

size_t i2s_output_stream_write(const uint8_t *data, size_t len) {
  if (len == 0 || player_task_handle == NULL) return 0;
  size_t written = 0;

  // Scale volume
  if (s_volume_factor < 0.99f) {
    if (s_bits_per_sample == 16) {
      int16_t *s = (int16_t *)data;
      for (size_t i = 0; i < len / 2; i++) s[i] = (int16_t)(s[i] * s_volume_factor);
    } else if (s_bits_per_sample == 32) {
      int32_t *s = (int32_t *)data;
      for (size_t i = 0; i < len / 4; i++) s[i] = (int32_t)(s[i] * s_volume_factor);
    }
  }

  size_t bytes_left = len;
  const uint8_t *curr = data;
  while (bytes_left > 0 && player_task_handle != NULL) {
    if (s_channels == 1 && s_bits_per_sample == 16) {
      int16_t stereo_buf[256 * 2];
      size_t samples = (bytes_left / 2 < 256) ? bytes_left / 2 : 256;
      for (size_t i = 0; i < samples; i++) {
        int16_t s = ((int16_t*)curr)[i];
        stereo_buf[i*2] = s; stereo_buf[i*2+1] = s;
      }
      size_t w = (size_t)i2s_output.write((uint8_t*)stereo_buf, samples * 4);
      curr += (w / 2); // approximate
      written += (w / 2);
      bytes_left -= (w / 2);
      if (w < samples * 4) break;
    } else {
      size_t to_write = (bytes_left < 512) ? bytes_left : 512;
      size_t w = (size_t)i2s_output.write((uint8_t *)curr, to_write);
      curr += w;
      written += w;
      bytes_left -= w;
      if (w < to_write) break;
    }
    vTaskDelay(1);
  }
  return written;
}


void i2s_output_stream_end(void) {
  // Gracefully stop I2S streaming; don't deinit completely so caller can
  // re-use playback functions.
  i2s_output.end();
}

void i2s_output_deinit(void)
{ 
    i2s_output.end(); 
}

//Initialize the audio interface
int audio_output_init(int bclk, int lrc, int dout) {
  i2s_output_init(bclk, lrc, dout);
  i2s_output_deinit();
  return audio.setPinout(bclk, lrc, dout);
}

//Set the volume: 0-21
void audio_output_set_volume(int volume) {
  if (volume < 0) volume = 0;
  if (volume > 21) volume = 21;
  s_volume_factor = (float)volume / 21.0f;
  audio.setVolume(volume);
}

//Query volume
int audio_read_output_volume(void) {
  return audio.getVolume();
}

//Pause/play the music
void audio_output_pause_resume(void) {
  audio.pauseResume();
}

//Stop the music
void audio_output_stop(void) {
  audio.stopSong();
}

//Whether the music is running
bool audio_output_is_running(void) {
  return audio.isRunning();
}

//Gets how long the music player has been playing
long audio_get_total_output_playing_time(void) {
  return (long)audio.getTotalPlayingTime() / 1000;
}

//Obtain the playing time of the music file
long audio_output_get_file_duration(void) {
  return (long)audio.getAudioFileDuration();
}

//Set play position
bool audio_output_set_play_position(int second) {
  return audio.setAudioPlayPosition((uint16_t)second);
}

//Gets the current playing time of the music
long audio_read_output_play_position(void) {
  return audio.getAudioCurrentTime();
}

//Non-blocking music execution function
void audio_output_loop(void) {
  audio.loop();
}

// optional
void audio_info(const char *info) {
  Serial.print("info        ");
  Serial.println(info);
}

void audio_eof_mp3(const char *info) {  
  Serial.print("eof_mp3     ");
  Serial.println(info);
}
