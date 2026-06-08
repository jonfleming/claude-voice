// driver_sd_card.h — SDMMC card driver for Waveshare ESP32-S3-Touch-AMOLED-1.8
//
// Hardware:
//   SD Card via SDMMC 1-bit mode
//   CLK=GPIO2, CMD=GPIO1, DATA=GPIO3
//
// Supports:
//   - Card detection and initialization
//   - File read/write operations
//   - Directory listing
//   - Card removal detection (hot-unplug)
//   - Works with 4GB, 8GB, 32GB SD cards (FAT32)
//
// Usage:
//   sd_card_init()         — Initialize SDMMC, mount filesystem
//   sd_card_is_ready()     — Check if card is present and mounted
//   sd_card_get_state()    — Get detailed state enum
//   sd_card_get_info()     — Get card size, free space, type
//   sd_card_free_space()   — Get free space in bytes
//   sd_card_total_space()  — Get total space in bytes
//   sd_card_file_exists()  — Check if file exists
//   sd_card_write_file()   — Write data to file
//   sd_card_read_file()    — Read file into buffer
//   sd_card_list_dir()     — List directory entries
//   sd_card_mkdir()        — Create directory (recursive)
//   sd_card_delete_file()  — Delete a file
//   sd_card_was_removed()  — Check if card was removed
//   sd_card_try_remount()  — Try to remount after removal
//   sd_card_deinit()       — Unmount filesystem and deinitialize SDMMC

#ifndef DRIVER_SD_CARD_H
#define DRIVER_SD_CARD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// =============================================================================
// SD card state
// =============================================================================
typedef enum {
    SD_STATE_UNINITIALIZED = 0,  // Driver not yet initialized
    SD_STATE_CARD_NOT_PRESENT,   // Card not inserted
    SD_STATE_MOUNT_FAILED,       // Card present but mount failed
    SD_STATE_READY,              // Card mounted and ready
    SD_STATE_REMOVED,            // Card was removed (hot-unplug detected)
} sd_card_state_t;

// =============================================================================
// SD card info (returned by sd_card_get_info)
// =============================================================================
typedef struct {
    uint64_t card_size_mb;      // Total card size in MB
    uint64_t free_space_mb;     // Free space in MB
    uint64_t used_space_mb;     // Used space in MB
    uint8_t  card_type;         // 0=none, 1=MMC, 2=SD1/SD2, 3=SDHC, 4=unknown
    char     card_type_str[16]; // Human-readable type string
} sd_card_info_t;

// =============================================================================
// File operation result codes
// =============================================================================
typedef enum {
    SD_FILE_OK = 0,
    SD_FILE_ERROR,
    SD_FILE_NOT_FOUND,
    SD_FILE_PERMISSION_DENIED,
    SD_FILE_DISK_FULL,
    SD_FILE_INVALID_PATH,
    SD_FILE_NOT_MOUNTED,
} sd_file_result_t;

// ---------- Public API ----------

/**
 * Initialize SDMMC card and mount FAT filesystem.
 *
 * Configures SDMMC host on GPIO2 (CLK), GPIO1 (CMD), GPIO3 (DATA0),
 * initializes the card, and mounts the FAT filesystem.
 *
 * @return true if card detected and filesystem mounted successfully
 */
bool sd_card_init(void);

/** True if SD card is present and filesystem is mounted. */
bool sd_card_is_ready(void);

/** Get the current SD card state. */
sd_card_state_t sd_card_get_state(void);

/** Get SD card information (size, free space, type). */
bool sd_card_get_info(sd_card_info_t* info);

/** Get free space on SD card in bytes. Returns -1 if not ready. */
int64_t sd_card_free_space(void);

/** Get total space on SD card in bytes. Returns -1 if not ready. */
int64_t sd_card_total_space(void);

/** Check if a file exists on the SD card. */
bool sd_card_file_exists(const char *path);

/**
 * Write data to a file on the SD card.
 * Creates the file if it doesn't exist, overwrites if it does.
 *
 * @param path File path relative to mount point (e.g., "/recording.wav")
 * @param data Pointer to data buffer
 * @param len Number of bytes to write
 * @return number of bytes written, or negative error code on error
 */
int sd_card_write_file(const char *path, const uint8_t *data, size_t len);

/**
 * Read a file from the SD card into a buffer.
 *
 * @param path File path relative to mount point
 * @param buf Output buffer
 * @param max_len Maximum number of bytes to read
 * @return number of bytes read, or negative error code on error
 */
int sd_card_read_file(const char *path, uint8_t *buf, size_t max_len);

/**
 * List directory entries.
 *
 * @param dir Directory path (e.g., "/")
 * @param entries Output array of filename strings (max 63 chars each)
 * @param max_count Maximum number of entries to return
 * @return number of entries listed, or negative error code on error
 */
int sd_card_list_dir(const char *dir, char entries[][64], int max_count);

/**
 * Create a directory on the SD card (and parent directories if needed).
 *
 * @param dir_path Directory path (e.g., "/data/audio")
 * @return true if directory was created or already exists
 */
bool sd_card_mkdir(const char *dir_path);

/**
 * Delete a file from the SD card.
 *
 * @param path File path relative to mount point
 * @return true if file was deleted, false if not found or error
 */
bool sd_card_delete_file(const char *path);

/**
 * Check if SD card was removed (hot-unplug detection).
 *
 * @return true if the card was previously mounted but is now gone
 */
bool sd_card_was_removed(void);

/**
 * Try to remount the SD card after removal.
 *
 * @return true if card is now mounted, false if still not present
 */
bool sd_card_try_remount(void);

/** Unmount filesystem and deinitialize SDMMC. */
void sd_card_deinit(void);

/** Get a human-readable string of the current SD card state. */
const char* sd_card_state_to_string(sd_card_state_t state);

#endif // DRIVER_SD_CARD_H
