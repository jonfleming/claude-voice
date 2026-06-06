// audio_ring_buffer.cpp
// Thread-safe ring buffer implementation for 16-bit mono audio.
// Uses volatile variables for inter-task communication (no mutex needed
// for single-producer/single-consumer pattern).

#include "audio_ring_buffer.h"

// --- Ring buffer operations ---

void audio_ring_buffer_init(audio_ring_buffer_t *rb) {
    rb->write_pos = 0;
    rb->read_pos = 0;
}

static inline uint32_t ring_pos_mod(audio_ring_buffer_t *rb, uint32_t pos) {
    // Power-of-2 capacity allows fast modulo via bitwise AND
    return pos & (AUDIO_RING_BUFFER_CAPACITY - 1);
}

uint32_t audio_ring_buffer_write(audio_ring_buffer_t *rb, const int16_t *src, uint32_t count) {
    uint32_t written = 0;
    
    while (written < count) {
        uint32_t wp = rb->write_pos;
        uint32_t rp = rb->read_pos;
        
        // Calculate free slots
        uint32_t free_slots;
        if (rp > wp) {
            free_slots = rp - wp - 1;  // Leave one slot empty to distinguish full vs empty
        } else {
            free_slots = AUDIO_RING_BUFFER_CAPACITY - wp + rp - 1;
        }
        
        if (free_slots == 0) {
            break;  // Ring buffer is full
        }
        
        // Calculate how many we can write in this burst
        uint32_t wp_mod = ring_pos_mod(rb, wp);
        uint32_t contig = AUDIO_RING_BUFFER_CAPACITY - wp_mod;  // contiguous bytes to end
        uint32_t burst = (contig < (free_slots < count - written ? free_slots : count - written)) 
                         ? contig : (free_slots < count - written ? free_slots : count - written);
        
        // Write samples
        for (uint32_t i = 0; i < burst; i++) {
            rb->buffer[wp_mod + i] = src[written + i];
        }
        
        // Update write position
        rb->write_pos = wp + burst;
        written += burst;
    }
    
    return written;
}

uint32_t audio_ring_buffer_read(audio_ring_buffer_t *rb, int16_t *dst, uint32_t count) {
    uint32_t read_count = 0;
    
    while (read_count < count) {
        uint32_t wp = rb->write_pos;
        uint32_t rp = rb->read_pos;
        
        // Calculate available samples
        uint32_t avail;
        if (wp > rp) {
            avail = wp - rp;
        } else {
            avail = (wp == rp) ? 0 : AUDIO_RING_BUFFER_CAPACITY - rp + wp;
        }
        
        if (avail == 0) {
            break;  // Ring buffer is empty
        }
        
        // Calculate how many we can read in this burst
        uint32_t rp_mod = ring_pos_mod(rb, rp);
        uint32_t contig = AUDIO_RING_BUFFER_CAPACITY - rp_mod;
        uint32_t burst = (contig < (avail < count - read_count ? avail : count - read_count))
                         ? contig : (avail < count - read_count ? avail : count - read_count);
        
        // Read samples
        for (uint32_t i = 0; i < burst; i++) {
            dst[read_count + i] = rb->buffer[rp_mod + i];
        }
        
        // Update read position
        rb->read_pos = rp + burst;
        read_count += burst;
    }
    
    return read_count;
}

uint32_t audio_ring_buffer_available(audio_ring_buffer_t *rb) {
    uint32_t wp = rb->write_pos;
    uint32_t rp = rb->read_pos;
    
    if (wp >= rp) {
        return wp - rp;
    } else {
        return AUDIO_RING_BUFFER_CAPACITY - rp + wp;
    }
}

uint32_t audio_ring_buffer_free(audio_ring_buffer_t *rb) {
    return AUDIO_RING_BUFFER_CAPACITY - 1 - audio_ring_buffer_available(rb);
}

bool audio_ring_buffer_is_empty(audio_ring_buffer_t *rb) {
    return rb->write_pos == rb->read_pos;
}

bool audio_ring_buffer_is_full(audio_ring_buffer_t *rb) {
    // Full when write_pos catches up to read_pos + capacity (modulo)
    // but we leave one slot empty, so check if available == capacity - 1
    return audio_ring_buffer_available(rb) >= (AUDIO_RING_BUFFER_CAPACITY - 1);
}

// --- Stereo to mono conversion ---

uint32_t audio_ring_buffer_convert_stereo_to_mono(
    const uint8_t *stereo_data,
    size_t stereo_bytes,
    int16_t *mono_output,
    uint32_t out_capacity,
    float gain,
    float dc_alpha
) {
    if (!stereo_data || !mono_output || stereo_bytes < 8) return 0;
    
    const int32_t *samples = (const int32_t *)stereo_data;
    size_t stereo_pairs = stereo_bytes / 8;  // 2 channels * 4 bytes per pair
    
    // DC removal state (static to preserve across calls)
    static float dc_offset = 0.0f;
    
    int16_t *out16 = mono_output;
    uint32_t out_samples = 0;
    
    for (size_t i = 0; i < stereo_pairs; i++) {
        // Sum L and R channels (handles mics on either channel)
        int32_t raw_sample = samples[i * 2] + samples[i * 2 + 1];
        
        // 1. Remove DC offset (high-pass filter)
        dc_offset = (dc_alpha * dc_offset) + ((1.0f - dc_alpha) * (float)raw_sample);
        float filtered = (float)raw_sample - dc_offset;
        
        // 2. Apply gain and scale from 32-bit to 16-bit
        float amplified = (filtered * gain) / 65536.0f;
        
        // 3. Clamp and store
        if (amplified > 32767.0f) amplified = 32767.0f;
        else if (amplified < -32768.0f) amplified = -32768.0f;
        
        // Check output capacity
        if (out_samples >= out_capacity) break;
        
        out16[out_samples++] = (int16_t)amplified;
    }
    
    return out_samples;
}
