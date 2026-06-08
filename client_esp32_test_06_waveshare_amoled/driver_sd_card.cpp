// SD card driver implementation for Waveshare ESP32-S3-Touch-AMOLED-1.8
// Uses ESP32 SD_MMC (SDMMC 1-bit) + Arduino FS API (ESP-IDF fatfs backend)
//
// Hardware pinout (Waveshare board):
//   SDMMC_CLK = GPIO2
//   SDMMC_CMD = GPIO1
//   SDMMC_DATA = GPIO3 (1-bit mode, only DAT0 used)
//
// The SD_MMC library in the ESP32 Arduino core (v3.x) wraps ESP-IDF's sdmmc_host
// and fatfs. It supports SD/SDHC/SDXC cards formatted as FAT32.
//
// API (ESP32 Arduino core v3.3.10):
//   SD_MMC.setPins(clk, cmd, d0)     - set SDMMC pins
//   SD_MMC.begin("/sdcard", mode1bit) - mount filesystem
//   SD_MMC.cardType()                 - returns sdcard_type_t
//   SD_MMC.cardSize()                 - returns card size in bytes
//   SD_MMC.totalBytes() / usedBytes() - for space info
//   SD_MMC.end()                      - unmount

#include "driver_sd_card.h"
#include "board_pins.h"

#ifdef BOARD_WAVESHARE_AMOLED

#include <Arduino.h>
#include <SD_MMC.h>
#include <FS.h>
#include <dirent.h>

// =============================================================================
// Internal state
// =============================================================================
static sd_card_state_t s_state = SD_STATE_UNINITIALIZED;
static bool s_init_complete = false;
static uint8_t s_last_error = 0;
static bool s_card_was_present = false;  // track previous presence for removal detection

// Card type strings (per ESP32 Arduino SD_MMC API)
static const char* card_type_strings[] = {
    "NONE",
    "MMC",
    "SD",
    "SDHC",
    "UNKNOWN"
};

// =============================================================================
// Helper: check if SD_MMC is still mounted (for removal detection)
// =============================================================================
static bool sd_card_is_mounted(void) {
    // Try to open a file on the SD_MMC FS; if unmounted, this fails
    File test = SD_MMC.open("/.probe", "r");
    bool mounted = test;
    if (test) test.close();
    return mounted;
}

// =============================================================================
// Public API implementation
// =============================================================================

bool sd_card_init(void) {
    Serial.println("[sdcard] Initializing SD card (SDMMC 1-bit mode)...");
    Serial.printf("[sdcard] Pins: CLK=GPIO%d, CMD=GPIO%d, DAT=GPIO%d\r\n",
        SDMMC_CLK, SDMMC_CMD, SDMMC_DATA);

    // Set SDMMC pins (required before begin())
    if (!SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA)) {
        Serial.println("[sdcard] [FAIL] setPins() failed!");
        s_state = SD_STATE_MOUNT_FAILED;
        s_last_error = 1;
        return false;
    }

    // Mount the SD card in 1-bit mode
    // mountpoint="/sdcard", mode1bit=true, format_if_mount_failed=false
    bool ok = SD_MMC.begin("/sdcard", true, false);

    if (!ok) {
        Serial.println("[sdcard] [FAIL] SD_MMC.begin() failed!");
        s_state = SD_STATE_CARD_NOT_PRESENT;
        s_last_error = 2;  // Card not present
        return false;
    }

    // Check card type
    sd_card_info_t info;
    if (!sd_card_get_info(&info)) {
        Serial.println("[sdcard] [FAIL] Could not read card info!");
        s_state = SD_STATE_MOUNT_FAILED;
        s_last_error = 3;  // Card info error
        SD_MMC.end();
        return false;
    }

    Serial.printf("[sdcard] [OK] Card detected: %s, %dMB\r\n",
        info.card_type_str, (uint32_t)info.card_size_mb);
    Serial.printf("[sdcard] [info] Free: %dMB, Used: %dMB\r\n",
        (uint32_t)(info.card_size_mb - info.used_space_mb),
        (uint32_t)info.used_space_mb);

    // Verify filesystem by checking root directory
    if (!SD_MMC.exists("/")) {
        Serial.println("[sdcard] [FAIL] Root directory not accessible!");
        s_state = SD_STATE_MOUNT_FAILED;
        s_last_error = 4;  // FS not accessible
        SD_MMC.end();
        return false;
    }

    s_state = SD_STATE_READY;
    s_card_was_present = true;
    s_last_error = 0;
    Serial.println("[sdcard] [OK] SD card mounted and ready.");
    return true;
}

sd_card_state_t sd_card_get_state(void) {
    // Check for hot-unplug using SD_MMC.cardType() (proper API, not probe files)
    if (s_state == SD_STATE_READY) {
        sdcard_type_t type = SD_MMC.cardType();
        if (type == CARD_NONE) {
            s_state = SD_STATE_REMOVED;
            s_card_was_present = false;
            Serial.println("[sdcard] [WARN] Card removed (hot-unplug detected via cardType).");
        }
    }
    return s_state;
}

bool sd_card_get_info(sd_card_info_t* info) {
    if (!info) return false;

    sdcard_type_t type = SD_MMC.cardType();
    info->card_size_mb = SD_MMC.cardSize() / (1024 * 1024);
    info->used_space_mb = SD_MMC.usedBytes() / (1024 * 1024);
    info->free_space_mb = (SD_MMC.cardSize() - SD_MMC.usedBytes()) / (1024 * 1024);
    info->card_type = (uint8_t)type;

    if (type < CARD_NONE || type > CARD_UNKNOWN) {
        strncpy(info->card_type_str, "UNKNOWN", sizeof(info->card_type_str) - 1);
        info->card_type_str[sizeof(info->card_type_str) - 1] = '\0';
    } else {
        strncpy(info->card_type_str, card_type_strings[type], sizeof(info->card_type_str) - 1);
        info->card_type_str[sizeof(info->card_type_str) - 1] = '\0';
    }

    return true;
}

sd_file_result_t sd_card_write_file(const char* path, const uint8_t* data, uint32_t length) {
    if (s_state != SD_STATE_READY) return SD_FILE_NOT_MOUNTED;
    if (!path || !data || length == 0) return SD_FILE_INVALID_PATH;

    // Validate path starts with /
    if (path[0] != '/') return SD_FILE_INVALID_PATH;

    File file = SD_MMC.open(path, FILE_WRITE);
    if (!file) {
        Serial.printf("[sdcard] [FAIL] Cannot open file for write: %s\r\n", path);
        return SD_FILE_ERROR;
    }

    uint32_t written = file.write(data, length);
    file.close();

    if (written != length) {
        Serial.printf("[sdcard] [FAIL] Write incomplete: wrote %lu/%lu bytes\r\n",
            (unsigned long)written, (unsigned long)length);
        return SD_FILE_DISK_FULL;
    }

    Serial.printf("[sdcard] [OK] Wrote %lu bytes to %s\r\n",
        (unsigned long)length, path);
    return SD_FILE_OK;
}

sd_file_result_t sd_card_read_file(const char* path, uint8_t* buffer, uint32_t buffer_size, uint32_t* bytes_read) {
    if (s_state != SD_STATE_READY) return SD_FILE_NOT_MOUNTED;
    if (!path || !buffer || buffer_size == 0) return SD_FILE_INVALID_PATH;
    if (path[0] != '/') return SD_FILE_INVALID_PATH;

    File file = SD_MMC.open(path, FILE_READ);
    if (!file) {
        Serial.printf("[sdcard] [FAIL] Cannot open file for read: %s\r\n", path);
        return SD_FILE_NOT_FOUND;
    }

    uint32_t available = file.size();
    uint32_t to_read = (available < buffer_size) ? available : buffer_size;
    uint32_t read = file.read(buffer, to_read);
    file.close();

    if (bytes_read) *bytes_read = read;

    if (read == 0) {
        return SD_FILE_NOT_FOUND;
    }

    Serial.printf("[sdcard] [OK] Read %lu bytes from %s\r\n",
        (unsigned long)read, path);
    return SD_FILE_OK;
}

bool sd_card_file_exists(const char* path) {
    if (s_state != SD_STATE_READY) return false;
    if (!path || path[0] != '/') return false;
    return SD_MMC.exists(path);
}

bool sd_card_delete_file(const char* path) {
    if (s_state != SD_STATE_READY) return false;
    if (!path || path[0] != '/') return false;

    bool ok = SD_MMC.remove(path);
    if (ok) {
        Serial.printf("[sdcard] [OK] Deleted file: %s\r\n", path);
    } else {
        Serial.printf("[sdcard] [FAIL] Cannot delete file: %s\r\n", path);
    }
    return ok;
}

uint32_t sd_card_list_directory(const char* dir_path, char filenames[][64], uint32_t max_count) {
    if (s_state != SD_STATE_READY) return 0;
    if (!dir_path || dir_path[0] != '/') return 0;

    // Validate directory exists
    if (!SD_MMC.exists(dir_path)) {
        Serial.printf("[sdcard] [FAIL] Directory not found: %s\r\n", dir_path);
        return 0;
    }

    // Use openDir() which returns an fs::Dir-like object in ESP32 Arduino v3.x
    File dir = SD_MMC.open(dir_path);
    if (!dir) {
        Serial.printf("[sdcard] [FAIL] Cannot open directory: %s\r\n", dir_path);
        return 0;
    }

    // Read directory entries using readdir() on the FS
    // ESP32 Arduino FS uses standard dirent API
    DIR* d = opendir(dir_path);
    if (!d) {
        dir.close();
        Serial.printf("[sdcard] [FAIL] Cannot opendir() directory: %s\r\n", dir_path);
        return 0;
    }

    uint32_t count = 0;
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr && count < max_count) {
        // Skip "." and ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        strncpy(filenames[count], entry->d_name, 63);
        filenames[count][63] = '\0';
        count++;
    }
    closedir(d);
    dir.close();

    Serial.printf("[sdcard] [info] Listed %u files in %s\r\n", count, dir_path);
    return count;
}

bool sd_card_mkdir(const char* dir_path) {
    if (s_state != SD_STATE_READY) return false;
    if (!dir_path || dir_path[0] != '/') return false;

    // Create parent directories recursively
    String path(dir_path);
    String parent;

    for (uint32_t i = 1; i < path.length(); i++) {
        if (path[i] == '/') {
            parent = path.substring(0, i);
            if (!SD_MMC.exists(parent.c_str())) {
                if (!SD_MMC.mkdir(parent.c_str())) {
                    Serial.printf("[sdcard] [FAIL] Cannot create directory: %s\r\n", parent.c_str());
                    return false;
                }
            }
        }
    }

    // Create the final directory
    if (!SD_MMC.exists(path.c_str())) {
        if (!SD_MMC.mkdir(path.c_str())) {
            Serial.printf("[sdcard] [FAIL] Cannot create directory: %s\r\n", path.c_str());
            return false;
        }
    }

    Serial.printf("[sdcard] [OK] Directory ready: %s\r\n", dir_path);
    return true;
}

bool sd_card_was_removed(void) {
    if (s_state == SD_STATE_REMOVED) {
        s_card_was_present = false;
        return true;
    }
    return false;
}

bool sd_card_try_remount(void) {
    Serial.println("[sdcard] Retrying SD card mount...");

    // Unmount first
    SD_MMC.end();
    delay(100);

    // Re-set pins and remount
    if (!SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA)) {
        Serial.println("[sdcard] [FAIL] setPins() failed during remount!");
        s_state = SD_STATE_MOUNT_FAILED;
        s_last_error = 1;
        return false;
    }

    // Wait for card to be detected (some cards need extra time)
    delay(50);

    bool ok = SD_MMC.begin("/sdcard", true, false);
    if (ok) {
        s_state = SD_STATE_READY;
        s_card_was_present = true;
        s_last_error = 0;
        Serial.println("[sdcard] [OK] Card remounted successfully.");
    } else {
        s_state = SD_STATE_CARD_NOT_PRESENT;
        s_card_was_present = false;
        s_last_error = 2;
        Serial.println("[sdcard] [FAIL] Card still not present.");
    }
    return ok;
}

bool sd_card_unmount(void) {
    if (s_state == SD_STATE_READY) {
        SD_MMC.end();
        s_state = SD_STATE_CARD_NOT_PRESENT;
        Serial.println("[sdcard] [OK] SD card unmounted.");
    } else {
        Serial.println("[sdcard] [info] SD card already unmounted.");
    }
    return true;
}

const char* sd_card_state_to_string(sd_card_state_t state) {
    switch (state) {
        case SD_STATE_UNINITIALIZED:    return "UNINITIALIZED";
        case SD_STATE_CARD_NOT_PRESENT: return "NO_CARD";
        case SD_STATE_MOUNT_FAILED:     return "MOUNT_FAILED";
        case SD_STATE_READY:            return "READY";
        case SD_STATE_REMOVED:          return "REMOVED";
        default:                        return "UNKNOWN";
    }
}

// =============================================================================
// Async init helpers (for non-blocking startup)
// =============================================================================

bool sd_card_init_async(void) {
    if (s_init_complete) return (s_state == SD_STATE_READY);

    // First attempt at init
    bool ok = sd_card_init();
    if (ok) {
        s_init_complete = true;
    }
    return ok;
}

bool sd_card_init_is_ready(void) {
    return s_init_complete && (s_state == SD_STATE_READY);
}

uint8_t sd_card_get_last_error(void) {
    return s_last_error;
}

#else  // BOARD_WAVESHARE_AMOLED

// Stub implementation for non-Waveshare boards
// SD card driver only available on BOARD_WAVESHARE_AMOLED

bool sd_card_init(void) { return false; }
sd_card_state_t sd_card_get_state(void) { return SD_STATE_CARD_NOT_PRESENT; }
bool sd_card_get_info(sd_card_info_t* info) { return false; }
sd_file_result_t sd_card_write_file(const char* path, const uint8_t* data, uint32_t length) { return SD_FILE_NOT_MOUNTED; }
sd_file_result_t sd_card_read_file(const char* path, uint8_t* buffer, uint32_t buffer_size, uint32_t* bytes_read) { return SD_FILE_NOT_MOUNTED; }
bool sd_card_file_exists(const char* path) { return false; }
bool sd_card_delete_file(const char* path) { return false; }
uint32_t sd_card_list_directory(const char* dir_path, char filenames[][64], uint32_t max_count) { return 0; }
bool sd_card_mkdir(const char* dir_path) { return false; }
bool sd_card_was_removed(void) { return false; }
bool sd_card_try_remount(void) { return false; }
bool sd_card_unmount(void) { return true; }
const char* sd_card_state_to_string(sd_card_state_t state) { return "NO_CARD"; }
bool sd_card_init_async(void) { return false; }
bool sd_card_init_is_ready(void) { return false; }
uint8_t sd_card_get_last_error(void) { return 0; }

#endif  // BOARD_WAVESHARE_AMOLED
