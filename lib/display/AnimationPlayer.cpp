#include "AnimationPlayer.hpp"
#include "DisplayDriver.hpp"
#include "esp_log.h"

static const char* TAG = "AnimationPlayer";

AnimationPlayer::AnimationPlayer(Framebuffer* fb, DisplayDriver* drv)
    : fb_(fb), drv_(drv)
{
    if (!fb_ || !drv_) {
        ESP_LOGE(TAG, "AnimationPlayer created with null fb or driver!");
    }
}

void AnimationPlayer::setAnimation(const Animation& anim, int x, int y)
{
    if (!anim.valid()) {
        ESP_LOGW(TAG, "setAnimation: invalid animation");
        stop();
        return;
    }

    current_anim_ = anim;
    pos_x_ = x;
    pos_y_ = y;

    frame_index_ = 0;
    frame_timer_ = 0;
    paused_ = false;
    playing_ = true;

    if (anim.fps == 0) {
        frame_interval_ = 50;  // fallback 20 fps
    } else {
        frame_interval_ = 1000 / anim.fps;
    }

    ESP_LOGI(TAG, "Animation set: %u frames, fps=%u, loop=%s",
             (unsigned)anim.frames.size(), anim.fps,
             anim.loop ? "true" : "false");
}

void AnimationPlayer::stop()
{
    playing_ = false;
    paused_  = false;
    frame_index_ = 0;
    frame_timer_ = 0;
}

void AnimationPlayer::pause()
{
    paused_ = true;
}

void AnimationPlayer::resume()
{
    paused_ = false;
}

void AnimationPlayer::update(uint32_t dt_ms)
{
    if (!playing_ || paused_ || !current_anim_.valid())
        return;

    frame_timer_ += dt_ms;

    // Chuyển frame theo thời gian
    while (frame_timer_ >= frame_interval_) {
        frame_timer_ -= frame_interval_;
        frame_index_++;

        if (frame_index_ >= current_anim_.frames.size()) {
            if (current_anim_.loop) {
                frame_index_ = 0;
            } else {
                // one-shot animation dừng ở frame cuối
                frame_index_ = current_anim_.frames.size() - 1;
                playing_ = false;
                break;
            }
        }
    }
}

void AnimationPlayer::render()
{
    if (!playing_ || !fb_ || !current_anim_.valid()) return;

    const AnimationFrame& fr = current_anim_.frames[frame_index_];

    if (fr.rgb == nullptr) {
        ESP_LOGW(TAG, "Frame missing RGB data");
        return;
    }

    if (fr.alpha) {
        // có alpha → dùng blend
        fb_->drawBitmapAlpha(pos_x_, pos_y_, fr.w, fr.h, fr.rgb, fr.alpha);
    } else {
        // không alpha → vẽ thẳng
        fb_->drawBitmap(pos_x_, pos_y_, fr.w, fr.h, fr.rgb);
    }
}
