#pragma once

/**
 * DeviceProfile
 * ------------------------------------------------------------------
 * Nhiệm vụ:
 *  - Cấu hình phần cứng (pin, i2s, adc, display)
 *  - Khởi tạo & wiring các module
 *  - Register assets (emotion / icon)
 *  - Gắn callback giữa module với AppController
 *
 * KHÔNG:
 *  - Không xử lý logic state
 *  - Không đọc/ghi NVS
 *  - Không có task loop
 */

class AppController;

class DeviceProfile
{
public:
    /**
     * Setup toàn bộ hệ thống trước khi AppController::start()
     *
     * @param app  AppController singleton
     * @return true nếu setup OK
     */
    static bool setup(AppController &app);
};
