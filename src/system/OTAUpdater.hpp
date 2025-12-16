#pragma once
#include <memory>
#include <functional>
#include "esp_ota_ops.h"

/**
 * Class:   OTAUpdater
 * Author:  Trung Nguyen
 * Email:   Trung.nt20217@gmail.com
 * Date:    17 Dec 2025
 * 
 * Description:
 * - Quản lý cập nhật firmware qua OTA
 * - Viết dữ liệu firmware vào partition OTA
 * - Kiểm tra và xác thực firmware
 * - Cung cấp callback tiến trình cập nhật
 * - Được điều khiển bởi AppController
 * - Không xử lý tải firmware từ mạng (NetworkManager làm việc đó)
 */
class OTAUpdater {
public:
    OTAUpdater() = default;
    ~OTAUpdater() = default;

    // Copy and move semantics
    OTAUpdater(const OTAUpdater&) = delete;
    OTAUpdater& operator=(const OTAUpdater&) = delete;
    OTAUpdater(OTAUpdater&&) = default;
    OTAUpdater& operator=(OTAUpdater&&) = default;

    // ======= Lifecycle =======
    bool init();
    void start();
    void stop();

    // ======= OTA Control =======
    /**
     * Begin OTA update with data buffer
     * @param data Pointer to firmware data
     * @param size Size of firmware data
     * @return true if update started successfully
     */
    bool beginUpdate(const uint8_t* data, size_t size);

    /**
     * Write data chunk to OTA partition
     * @param data Pointer to data chunk
     * @param size Size of data chunk
     * @return bytes written, -1 if error
     */
    int writeChunk(const uint8_t* data, size_t size);

    /**
     * Finish OTA update and validate
     * @return true if update completed successfully
     */
    bool finishUpdate();

    /**
     * Abort OTA update and rollback
     */
    void abortUpdate();

    // ======= Status =======
    bool isUpdating() const { return updating; }
    uint32_t getBytesWritten() const { return bytes_written; }
    uint32_t getTotalBytes() const { return total_bytes; }
    /**
     * Get current progress percentage (0-100)
     * @return progress percentage, 0 if no update in progress
     */
    uint8_t getProgressPercent() const;

    /**
     * Check if enough storage space for firmware update
     * @param firmware_size Size of firmware to update
     * @return true if storage is sufficient
     */
    bool checkStorageSpace(size_t firmware_size);

    /**
     * Get available free space in OTA partition
     * @return free space in bytes
     */
    uint32_t getAvailableSpace() const;

    // ======= Callbacks =======
    using ProgressCallback = std::function<void(uint32_t current, uint32_t total)>;
    void setProgressCallback(ProgressCallback cb) { progress_callback = cb; }

private:
    // ======= Internal state =======
    bool updating = false;
    uint32_t bytes_written = 0;
    uint32_t total_bytes = 0;

    // ======= ESP32 OTA handle =======
    esp_ota_handle_t update_handle = 0;
    const esp_partition_t* update_partition = nullptr;

    // ======= Callback =======
    ProgressCallback progress_callback;

    // ======= Helper functions =======
    bool validateFirmware();
    void reportProgress();
};
