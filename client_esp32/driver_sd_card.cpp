// driver_sd_card.cpp — SDMMC card driver for Waveshare ESP32-S3-Touch-AMOLED-1.8
//
// Hardware:
//   SD Card via SDMMC 1-bit mode
//   CLK=GPIO2, CMD=GPIO1, DATA=GPIO3
//
// Uses ESP32 SD_MMC library (wraps ESP-IDF sdmmc_host + fatfs)
// Supports SD/SDHC/SDXC cards formatted as FAT32.

#include "driver_sd_card.h"
#include "board_pins.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include <FS.h>
#include <dirent.h>
#include <string.h>

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
// sd_card_init
// =============================================================================
bool sd_card_init(void) {
    Serial.println("[sd] Initializing SD card (SDMMC 1-bit mode)...");
    Serial.printf("[sd] Pins: CLK=GPIO%d, CMD=GPIO%d, DAT=GPIO%d\r\n",
        SDMMC_CLK, SDMMC_CMD, SDMMC_DATA);

    // Set SDMMC pins (required before begin())
    if (!SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA)) {
        Serial.println("[sd] [FAIL] setPins() failed!");
        s_state = SD_STATE_MOUNT_FAILED;
        s_last_error = 1;
        return false;
    }

    // Mount the SD card in 1-bit mode
    // mountpoint="/sdcard", mode1bit=true, format_if_mount_failed=false
    bool ok = SD_MMC.begin("/sdcard", true, false);

    if (!ok) {
        Serial.println("[sd] [FAIL] SD_MMC.begin() failed!");
        s_state = SD_STATE_CARD_NOT_PRESENT;
        s_last_error = 2;  // Card not present
        return false;
    }

    // Check card type
    sd_card_info_t info;
    if (!sd_card_get_info(&info)) {
        Serial.println("[sd] [FAIL] Could not read card info!");
        s_state = SD_STATE_MOUNT_FAILED;
        s_last_error = 3;
        SD_MMC.end();
        return false;
    }

    s_state = SD_STATE_READY;
    s_card_was_present = true;
    s_last_error = 0;

    Serial.printf("[sd] [OK] Card detected: %s, %dMB\r\n",
        info.card_type_str, (uint32_t)info.card_size_mb);
    Serial.printf("[sd] [info] Free: %dMB, Used: %dMB\r\n",
        (uint32_t)(info.card_size_mb - info.used_space_mb),
        (uint32_t)info.used_space_mb);

    // Verify filesystem by checking root directory
    if (!SD_MMC.exists("/")) {
        Serial.println("[sd] [FAIL] Root directory not accessible!");
        s_state = SD_STATE_MOUNT_FAILED;
        s_last_error = 4;
        SD_MMC.end();
        return false;
    }

    Serial.println("[sd] [OK] SD card mounted and ready.");
    return true;
}

// =============================================================================
// sd_card_is_ready
// =============================================================================
bool sd_card_is_ready(void) {
    // Also check for removal
    sd_card_get_state();
    return (s_state == SD_STATE_READY);
}

// =============================================================================
// sd_card_get_state
// =============================================================================
sd_card_state_t sd_card_get_state(void) {
    // Check for hot-unplug using SD_MMC.cardType() (proper API, not probe files)
    if (s_state == SD_STATE_READY) {
        sdcard_type_t type = SD_MMC.cardType();
        if (type == CARD_NONE) {
            s_state = SD_STATE_REMOVED;
            s_card_was_present = false;
            Serial.println("[sd] [WARN] Card removed (hot-unplug detected via cardType).");
        }
    }
    return s_state;
}

// =============================================================================
// sd_card_get_info
// =============================================================================
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

// =============================================================================
// Space queries
// =============================================================================
int64_t sd_card_free_space(void) {
    if (s_state != SD_STATE_READY) return -1;
    return (int64_t)(SD_MMC.cardSize() - SD_MMC.usedBytes());
}

int64_t sd_card_total_space(void) {
    if (s_state != SD_STATE_READY) return -1;
    return (int64_t)SD_MMC.cardSize();
}

// =============================================================================
// File operations
// =============================================================================
bool sd_card_file_exists(const char *path) {
    if (s_state != SD_STATE_READY) return false;
    if (!path || path[0] != '/') return false;
    return SD_MMC.exists(path);
}

int sd_card_write_file(const char *path, const uint8_t *data, size_t len) {
    if (s_state != SD_STATE_READY) return -1;
    if (!path || !data || len == 0) return -1;
    if (path[0] != '/') return -1;

    File file = SD_MMC.open(path, FILE_WRITE);
    if (!file) {
        Serial.printf("[sd] [FAIL] Cannot open file for write: %s\r\n", path);
        return -1;
    }

    size_t written = file.write(data, len);
    file.close();

    if (written != len) {
        Serial.printf("[sd] [FAIL] Write incomplete: wrote %lu/%lu bytes\r\n",
            (unsigned long)written, (unsigned long)len);
        return -1;
    }

    Serial.printf("[sd] [OK] Wrote %lu bytes to %s\r\n", (unsigned long)len, path);
    return (int)written;
}

int sd_card_read_file(const char *path, uint8_t *buf, size_t max_len) {
    if (s_state != SD_STATE_READY) return -1;
    if (!path || !buf || max_len == 0) return -1;
    if (path[0] != '/') return -1;

    File file = SD_MMC.open(path, FILE_READ);
    if (!file) {
        Serial.printf("[sd] [FAIL] Cannot open file for read: %s\r\n", path);
        return -1;
    }

    size_t available = file.size();
    size_t to_read = (available < max_len) ? available : max_len;
    size_t read = file.read(buf, to_read);
    file.close();

    if (read == 0) {
        return -1;
    }

    Serial.printf("[sd] [OK] Read %lu bytes from %s\r\n", (unsigned long)read, path);
    return (int)read;
}

int sd_card_list_dir(const char *dir_path, char filenames[][64], int max_count) {
    if (s_state != SD_STATE_READY) return -1;
    if (!dir_path || dir_path[0] != '/') return -1;

    // Validate directory exists
    if (!SD_MMC.exists(dir_path)) {
        Serial.printf("[sd] [FAIL] Directory not found: %s\r\n", dir_path);
        return -1;
    }

    DIR* d = opendir(dir_path);
    if (!d) {
        Serial.printf("[sd] [FAIL] Cannot opendir() directory: %s\r\n", dir_path);
        return -1;
    }

    int count = 0;
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

    Serial.printf("[sd] [info] Listed %d files in %s\r\n", count, dir_path);
    return count;
}

bool sd_card_mkdir(const char *dir_path) {
    if (s_state != SD_STATE_READY) return false;
    if (!dir_path || dir_path[0] != '/') return false;

    // Create parent directories recursively
    String path(dir_path);
    String parent;

    for (size_t i = 1; i < path.length(); i++) {
        if (path[i] == '/') {
            parent = path.substring(0, i);
            if (!SD_MMC.exists(parent.c_str())) {
                if (!SD_MMC.mkdir(parent.c_str())) {
                    Serial.printf("[sd] [FAIL] Cannot create directory: %s\r\n", parent.c_str());
                    return false;
                }
            }
        }
    }

    // Create the final directory
    if (!SD_MMC.exists(path.c_str())) {
        if (!SD_MMC.mkdir(path.c_str())) {
            Serial.printf("[sd] [FAIL] Cannot create directory: %s\r\n", path.c_str());
            return false;
        }
    }

    Serial.printf("[sd] [OK] Directory ready: %s\r\n", dir_path);
    return true;
}

bool sd_card_delete_file(const char *path) {
    if (s_state != SD_STATE_READY) return false;
    if (!path || path[0] != '/') return false;

    bool ok = SD_MMC.remove(path);
    if (ok) {
        Serial.printf("[sd] [OK] Deleted file: %s\r\n", path);
    } else {
        Serial.printf("[sd] [FAIL] Cannot delete file: %s\r\n", path);
    }
    return ok;
}

// =============================================================================
// Removal detection and remount
// =============================================================================
bool sd_card_was_removed(void) {
    if (s_state == SD_STATE_REMOVED) {
        s_card_was_present = false;
        return true;
    }
    return false;
}

bool sd_card_try_remount(void) {
    Serial.println("[sd] Retrying SD card mount...");

    // Unmount first
    SD_MMC.end();
    delay(100);

    // Re-set pins and remount
    if (!SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA)) {
        Serial.println("[sd] [FAIL] setPins() failed during remount!");
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
        Serial.println("[sd] [OK] Card remounted successfully.");
    } else {
        s_state = SD_STATE_CARD_NOT_PRESENT;
        s_card_was_present = false;
        s_last_error = 2;
        Serial.println("[sd] [FAIL] Card still not present.");
    }
    return ok;
}

// =============================================================================
// Deinit
// =============================================================================
void sd_card_deinit(void) {
    SD_MMC.end();
    s_state = SD_STATE_CARD_NOT_PRESENT;
    s_card_was_present = false;
    s_init_complete = false;
    Serial.println("[sd] SD card deinitialized.");
}

// =============================================================================
// State string
// =============================================================================
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
