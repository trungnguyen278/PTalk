#pragma once
#include <cstdint>

/*
===============================================================================
 RLE FORMAT FOR ESP-IDF DISPLAY ANIMATION
===============================================================================

Định dạng RLE này được thiết kế để:

✔ Vẽ cực nhanh (ít CPU, không cần giải mã JPEG)
✔ Tiết kiệm RAM (không cần buffer lớn)
✔ Đủ nhẹ để chứa nhiều animation trong Flash
✔ Hoạt động tốt với ST7789/SPI
✔ Chạy animation mượt 30–60 FPS
✔ Hỗ trợ nhiều animation (AnimationSet)

Cấu trúc:

1. Một animation gồm nhiều frame (RLEFrame)
2. Mỗi frame gồm nhiều segment (RLESegment)
3. Mỗi segment mô tả 1 dải pixel liên tiếp cùng màu RGB565

NOTE:
 - Các pixel được vẽ theo thứ tự quét từ trái sang phải, từ trên xuống dưới
 - Dữ liệu RLE animation thường được tạo bằng tool Python convert GIF→RLE
 - Animation có thể lưu ở Flash (.h file) hoặc SPIFFS (.bin RLE)
===============================================================================
*/

// Một segment RLE: "count" pixel liên tiếp có cùng màu "color"
struct RLESegment {
    uint16_t count;    // Số pixel liên tiếp
    uint16_t color;    // Màu RGB565
};

// Một frame RLE
struct RLEFrame {
    const RLESegment* segments; // Con trỏ tới mảng segment
    uint32_t num_segments;      // Số segment trong frame
};

// Một animation RLE hoàn chỉnh
struct RLEAnimation {
    const RLEFrame* frames;     // Danh sách các frame
    uint16_t num_frames;        // Tổng số frame
    uint16_t width;             // Chiều rộng khung hình
    uint16_t height;            // Chiều cao khung hình
};


/*
===============================================================================
 RLEAnimationSet
===============================================================================

Một State có thể chứa nhiều animation (ví dụ: IDLE có 3 animation khác nhau).
Ta gom chúng vào 1 nhóm → RLEAnimationSet.

Ứng dụng:
 - UIStateController có thể gọi playRandom() để chọn animation ngẫu nhiên
 - Các state SPEAKING có thể chứa nhiều animation theo emotion
===============================================================================
*/

struct RLEAnimationSet {
    const RLEAnimation** list;  // Mảng con trỏ tới animation
    uint16_t count;             // Số animation trong list
};
