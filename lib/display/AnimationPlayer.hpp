#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include "Framebuffer.hpp"

/*
 * AnimationFrame
 * ---------------------------------------------------------
 * Một frame animation: chứa
 *  - width, height
 *  - con trỏ tới RGB565 buffer
 *  - optional alpha mask (8 bit)
 */

struct AnimationFrame {
    int w = 0;
    int h = 0;
    const uint16_t* rgb = nullptr;
    const uint8_t*  alpha = nullptr;   // nullptr nếu frame không có alpha

    AnimationFrame() = default;
    AnimationFrame(int _w, int _h, const uint16_t* _rgb, const uint8_t* _alpha)
        : w(_w), h(_h), rgb(_rgb), alpha(_alpha) {}
};


/*
 * Animation
 * ---------------------------------------------------------
 * Một animation hoàn chỉnh:
 *  - danh sách frames
 *  - fps
 *  - loop hay one-shot
 */
struct Animation {
    std::vector<AnimationFrame> frames;
    uint16_t fps = 20;
    bool loop = true;

    bool valid() const { return !frames.empty(); }
};


/*
 * AnimationPlayer
 * ---------------------------------------------------------
 * Thực hiện:
 *  - set animation
 *  - update(dt_ms)
 *  - render vào framebuffer (ở vị trí x,y)
 *  - pause/resume
 *
 * Note: Không dùng timer riêng — DisplayManager sẽ gọi update().
 */

class AnimationPlayer {
public:
    AnimationPlayer(Framebuffer* fb, class DisplayDriver* drv);
    ~AnimationPlayer() = default;

    // Set animation mới
    void setAnimation(const Animation& anim, int x = 0, int y = 0);

    // Stop (không vẽ gì)
    void stop();

    // Resume/Pause
    void pause();
    void resume();
    bool isPaused() const { return paused_; }

    // Update theo thời gian (ms)
    void update(uint32_t dt_ms);

    // Vẽ frame hiện tại vào framebuffer
    void render();

private:
    Framebuffer* fb_ = nullptr;
    DisplayDriver* drv_ = nullptr;

    Animation current_anim_;
    int pos_x_ = 0;
    int pos_y_ = 0;

    uint32_t frame_timer_ = 0;   // tăng theo dt
    uint32_t frame_interval_ = 50; // ms/frame (20 fps)

    size_t frame_index_ = 0;

    bool paused_ = false;
    bool playing_ = false;
};
