#pragma once

#include <string>
#include <vector>
#include "Display.hpp"
#include "RLEAnimation.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*
===============================================================================
 DisplayAnimator (RLE Version)
===============================================================================

Nhiệm vụ của DisplayAnimator:

✔ Render frame RLE lên Display
✔ Play animation RLE theo FPS mong muốn
✔ Hỗ trợ loop hoặc non-loop
✔ Chạy trong 1 FreeRTOS task riêng biệt (do app_main tạo)
✔ Hỗ trợ AnimationSet:
    - Chọn animation theo index
    - Chọn animation ngẫu nhiên (playRandom)
✔ Không chứa UI logic, không chứa mapping state
✔ Dễ dùng cho UIStateController

Ứng dụng:
    DisplayAnimator anim(&display);
    anim.setAnimationSet(&idleSet);
    anim.playRandom(true, 15);
===============================================================================
*/

class DisplayAnimator {
public:
    explicit DisplayAnimator(Display* d);
    ~DisplayAnimator();

    // ========================================================================
    // BASIC RENDERING HELPERS
    // ========================================================================
    void clear(uint16_t color = 0x0000);

    void showText(const std::string& text,
                  int x = 0,
                  int y = 0,
                  int size = 2,
                  uint16_t color = 0xFFFF);

    void showBitmap(const uint16_t* data, int w, int h);

    // ========================================================================
    // ANIMATION SOURCE (RLE)
    // ========================================================================

    // Set 1 animation duy nhất
    bool setAnimation(const RLEAnimation* anim);

    // Set 1 AnimationSet (nhiều animation để random/chọn theo index)
    bool setAnimationSet(const RLEAnimationSet* set);

    // Bỏ animation hiện tại
    void clearAnimation();

    // ========================================================================
    // ANIMATION CONTROL
    // ========================================================================
    void play(bool loop = true, int fps = 15);

    // Chọn animation ngẫu nhiên trong set rồi play
    void playRandom(bool loop = true, int fps = 15);

    // Chọn animation theo index trong set rồi play
    void playIndex(int idx, bool loop = true, int fps = 15);

    void stop();

    bool isRunning() const { return running; }

    int frameCount() const;

    // ========================================================================
    // TASK ENTRY POINT
    //  - app_main() sẽ tạo task:
    //
    //      xTaskCreatePinnedToCore(
    //          DisplayAnimator::taskEntry,
    //          "anim_task",
    //          4096,
    //          &animator,
    //          4,
    //          NULL,
    //          1  // core 1
    //      );
    //
    // ========================================================================
    static void taskEntry(void* param);
    void taskLoop();

private:
    Display* display = nullptr;

    // Animation hiện tại
    const RLEAnimation* currentAnim = nullptr;

    // AnimationSet (nếu đang dùng)
    const RLEAnimationSet* currentSet = nullptr;

    // Animation state
    int frameIndex = 0;
    bool loop = true;
    int fps = 15;
    bool running = false;

    // Internal render 1 frame RLE
    bool drawRLEFrame(int idx);

    // Pick random animation trong currentSet
    const RLEAnimation* pickRandom() const;
};
