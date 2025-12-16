#include "OTAUpdater.hpp"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"

static const char* TAG = "OTAUpdater";

bool OTAUpdater::init() {
    ESP_LOGI(TAG, "OTAUpdater init()");
    return true;
}

void OTAUpdater::start() {
    ESP_LOGI(TAG, "OTAUpdater started");
}

void OTAUpdater::stop() {
    ESP_LOGI(TAG, "OTAUpdater stopped");
}

bool OTAUpdater::beginUpdate(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        ESP_LOGE(TAG, "Invalid firmware data");
        return false;
    }

    if (updating) {
        ESP_LOGW(TAG, "Update already in progress");
        return false;
    }

    // Check storage space before starting update
    if (!checkStorageSpace(size)) {
        ESP_LOGE(TAG, "Insufficient storage space for firmware update");
        return false;
    }

    // Find next OTA partition
    update_partition = esp_ota_get_next_update_partition(nullptr);
    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA partition found");
        return false;
    }

    ESP_LOGI(TAG, "Writing OTA partition at offset 0x%x", update_partition->address);

    // Begin OTA update
    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        return false;
    }

    updating = true;
    bytes_written = 0;
    total_bytes = size;

    ESP_LOGI(TAG, "OTA update started, total size: %u bytes", total_bytes);
    reportProgress();

    return true;
}

int OTAUpdater::writeChunk(const uint8_t* data, size_t size) {
    if (!data || size == 0 || !updating) {
        ESP_LOGE(TAG, "Invalid write: data=%p, size=%zu, updating=%d", data, size, updating);
        return -1;
    }

    esp_err_t err = esp_ota_write(update_handle, data, size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
        return -1;
    }

    bytes_written += size;
    reportProgress();

    return size;
}

bool OTAUpdater::finishUpdate() {
    if (!updating) {
        ESP_LOGW(TAG, "No update in progress");
        return false;
    }

    // End OTA update
    esp_err_t err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        updating = false;
        return false;
    }

    // Validate firmware
    if (!validateFirmware()) {
        ESP_LOGE(TAG, "Firmware validation failed");
        updating = false;
        return false;
    }

    // Set boot partition
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        updating = false;
        return false;
    }

    ESP_LOGI(TAG, "OTA update finished successfully");
    updating = false;

    return true;
}

void OTAUpdater::abortUpdate() {
    if (updating) {
        ESP_LOGW(TAG, "Aborting OTA update");
        esp_ota_abort(update_handle);
        updating = false;
        bytes_written = 0;
        total_bytes = 0;
    }
}

bool OTAUpdater::validateFirmware() {
    if (!update_partition) {
        ESP_LOGE(TAG, "No update partition");
        return false;
    }

    // Get running partition for comparison
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (!running) {
        ESP_LOGE(TAG, "No running partition");
        return false;
    }

    ESP_LOGI(TAG, "Running partition label: %s", running->label);
    ESP_LOGI(TAG, "Update partition label: %s", update_partition->label);

    // Basic validation: check partition is valid
    // More advanced: verify signature, checksum, etc.
    if (update_partition->address == 0 || update_partition->size == 0) {
        ESP_LOGE(TAG, "Invalid partition address or size");
        return false;
    }

    ESP_LOGI(TAG, "Firmware validation passed");
    return true;
}

void OTAUpdater::reportProgress() {
    if (progress_callback && total_bytes > 0) {
        progress_callback(bytes_written, total_bytes);
    }

    // Log progress every 10%
    static uint32_t last_percent = 0;
    if (total_bytes > 0) {
        uint32_t current_percent = (bytes_written * 100) / total_bytes;
        if (current_percent >= last_percent + 10) {
            ESP_LOGI(TAG, "OTA progress: %u%%", current_percent);
            last_percent = current_percent;
        }
    }
}

uint8_t OTAUpdater::getProgressPercent() const {
    if (!updating || total_bytes == 0) {
        return 0;
    }
    uint32_t percent = (bytes_written * 100) / total_bytes;
    return (percent > 100) ? 100 : percent;
}

bool OTAUpdater::checkStorageSpace(size_t firmware_size) {
    // Get the partition to check space
    const esp_partition_t* ota_partition = esp_ota_get_next_update_partition(nullptr);
    if (!ota_partition) {
        ESP_LOGE(TAG, "No OTA partition available");
        return false;
    }

    // Check if firmware size fits in partition
    if (firmware_size > ota_partition->size) {
        ESP_LOGE(TAG, "Firmware size (%zu bytes) exceeds partition size (%u bytes)",
                 firmware_size, ota_partition->size);
        return false;
    }

    ESP_LOGI(TAG, "Storage check: firmware=%zu bytes, partition=%u bytes - OK",
             firmware_size, ota_partition->size);
    return true;
}

uint32_t OTAUpdater::getAvailableSpace() const {
    const esp_partition_t* ota_partition = esp_ota_get_next_update_partition(nullptr);
    if (!ota_partition) {
        ESP_LOGW(TAG, "No OTA partition available");
        return 0;
    }
    return ota_partition->size;
}
