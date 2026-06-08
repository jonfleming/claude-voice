// SD card driver for Waveshare ESP32-S3-Touch-AMOLED-1.8
// Uses ESP32 SDMMC (1-bit mode) via ESP-IDF fatfs
//
// Hardware:
//   CLK = GPIO2, CMD = GPIO1, DAT = GPIO3 (1-bit SDMMC)
//   CS = auto-managed by SDMMC peripheral
//
// Supports:
//   - Card detection and initialization
//   - File read/write operations
//   - Directory listing
//   - Card removal detection
//   - Works with 4GB, 8GB, 32GB SD cards (FAT32)
//
// Notes:
//   - SDMMC uses the ESP32-S3 SD host controller
//   - No I2C conflicts (SDMMC is a separate bus from I2C)
//   - SDMMC_CLK (GPIO2) and SDMMC_CMD (GPIO1) are NOT used by I2C on this board
//   - SDMMC_DATA (GPIO3) is also free (I2C uses GPIO15/14)
//   - Requires FAT32-formatted card
//   - Mounts at "/sdcard" path (ESP-IDF fatfs convention)

#ifndef DRIVER_SD_CARD_H
#define DRIVER_SD_CARD_H

#include <stdint.h>
#include <stdbool.h>
#include <string>

// =============================================================================
// SD card mount path (ESP-IDF fatfs convention)
// =============================================================================
#define SD_CARD_MOUNT_PATH "/sdcard"

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
    uint8_t  card_type;         // 0=unknown, 1=SD1/SD2, 2=SDHC
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

// =============================================================================
// Public API
// =============================================================================

/**
 * Initialize the SD card driver.
 *
 * Performs:
 *   1. SDMMC bus setup (GPIO2/CLK, GPIO1/CMD, GPIO3/DAT)
 *   2. Card detection and mount
 *   3. FAT filesystem check
 *
 * @return true if card is mounted and ready, false otherwise
 */
bool sd_card_init(void);

/**
 * Get the current SD card state.
 *
 * @return current state enum value
 */
sd_card_state_t sd_card_get_state(void);

/**
 * Get SD card information (size, free space, type).
 *
 * @param info pointer to sd_card_info_t to fill
 * @return true if info was filled successfully
 */
bool sd_card_get_info(sd_card_info_t* info);

/**
 * Write a file to the SD card.
 *
 * Creates the file if it doesn't exist, overwrites if it does.
 *
 * @param path relative to mount point (e.g., "/data/audio.wav")
 * @param data pointer to data buffer
 * @param length number of bytes to write
 * @return result code (SD_FILE_OK on success)
 */
sd_file_result_t sd_card_write_file(const char* path, const uint8_t* data, uint32_t length);

/**
 * Read a file from the SD card.
 *
 * @param path relative to mount point (e.g., "/data/config.json")
 * @param buffer pointer to output buffer
 * @param buffer_size maximum bytes to read
 * @param bytes_read pointer to store actual bytes read (can be NULL)
 * @return result code (SD_FILE_OK on success)
 */
sd_file_result_t sd_card_read_file(const char* path, uint8_t* buffer, uint32_t buffer_size, uint32_t* bytes_read);

/**
 * Check if a file exists on the SD card.
 *
 * @param path relative to mount point
 * @return true if file exists
 */
bool sd_card_file_exists(const char* path);

/**
 * Delete a file from the SD card.
 *
 * @param path relative to mount point
 * @return true if file was deleted, false if not found or error
 */
bool sd_card_delete_file(const char* path);

/**
 * List files in a directory on the SD card.
 *
 * @param dir_path directory to list (e.g., "/data")
 * @param filenames output array of filename strings
 * @param max_count maximum number of filenames to return
 * @return number of files listed (0 if directory not found or empty)
 */
uint32_t sd_card_list_directory(const char* dir_path, char filenames[][64], uint32_t max_count);

/**
 * Create a directory on the SD card (and parent directories if needed).
 *
 * @param dir_path directory path (e.g., "/data/audio")
 * @return true if directory was created or already exists
 */
bool sd_card_mkdir(const char* dir_path);

/**
 * Check if SD card was removed (hot-unplug detection).
 *
 * Returns true if the card was previously mounted but is now gone.
 * Resets the state to SD_STATE_REMOVED after calling.
 *
 * @return true if card was removed
 */
bool sd_card_was_removed(void);

/**
 * Try to remount the SD card after removal.
 *
 * @return true if card is now mounted, false if still not present
 */
bool sd_card_try_remount(void);

/**
 * Unmount the SD card (safe to call multiple times).
 *
 * @return true if unmount succeeded or was already unmounted
 */
bool sd_card_unmount(void);

/**
 * Get a human-readable string of the current SD card state.
 *
 * @return static string (caller should copy if needed)
 */
const char* sd_card_state_to_string(sd_card_state_t state);

// =============================================================================
// Forward declarations for Arduino sketch integration
// =============================================================================
// These are implemented in driver_sd_card.cpp and called from the main sketch.

// Initialize SD card (non-blocking: returns immediately, uses internal timer for retries)
extern bool sd_card_init_async(void);

// Check if async init is complete
extern bool sd_card_init_is_ready(void);

// Get last init error code (0 = no error)
extern uint8_t sd_card_get_last_error(void);

#endif  // DRIVER_SD_CARD_H
