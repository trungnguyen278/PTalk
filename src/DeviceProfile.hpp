#pragma once

/**
 * Class:   DeviceProfile
 * Author:  Trung Nguyen
 * Email:
 * Date:    17 Dec 2025
 * 
 * Description:
 *  - Cấu hình thiết bị cụ thể trước khi khởi động ứng dụng
 *  - Thiết lập các tham số phần cứng, khởi tạo module
 *  - Được gọi từ AppController trước khi start()
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
