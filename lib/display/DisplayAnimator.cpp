// DisplayAnimator.cpp
#include "DisplayAnimator.hpp"
#include "esp_log.h"
#include <algorithm>
#include <vector>

static const char* TAG = "DisplayAnimator";


// -----------------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------------
DisplayAnimator::DisplayAnimator(Display* d)
: display(d)
{
}

DisplayAnimator::~DisplayAnimator() {
    stop();   // Không xoá task; task do app_main quản lý
}


// -----------------------------------------------------------------------------
// BASIC RENDERING HELPERS
// -----------------------------------------------------------------------------
void DisplayAnimator::clear(uint16_t color) {
    if (display) display->fill(color);
}

void DisplayAnimator::showText(const std::string& text,
                               int x,
                               int y,
                               int size,
                               uint16_t color)
{
    if (!display) return;
    display->clear(0x0000);
    display->drawText(text, x, y, size, color);
}

void DisplayAnimator::showBitmap(const uint16_t* data, int w, int h) {
    if (!display) return;
    display->drawBitmapRGB565(0, 0, w, h, data);
}


// -----------------------------------------------------------------------------
// Animation source setters
// -----------------------------------------------------------------------------
bool DisplayAnimator::setAnimation(const RLEAnimation* a) {
    if (!a) return false;
    stop();
    currentAnim = a;
    currentSet  = nullptr;
    frameIndex  = 0;
    return true;
}

bool DisplayAnimator::setAnimationSet(const RLEAnimationSet* set) {
    if (!set || set->count == 0 || !set->list) return false;
    stop();
    currentSet  = set;
    currentAnim = nullptr;
    frameIndex  = 0;
    return true;
}

void DisplayAnimator::clearAnimation() {
    stop();
    currentAnim = nullptr;
    currentSet  = nullptr;
    frameIndex  = 0;
}


// -----------------------------------------------------------------------------
// Control
// -----------------------------------------------------------------------------
void DisplayAnimator::play(bool _loop, int _fps) {
    loop = _loop;
    fps  = std::max(1, _fps);
    running = true;
    frameIndex = 0;

    // Nếu có set mà chưa chọn animation → chọn cái đầu
    if (!currentAnim && currentSet && currentSet->count > 0) {
        currentAnim = currentSet->list[0];
    }

    ESP_LOGI(TAG, "Play animation: fps=%d loop=%d", fps, loop ? 1 : 0);
}

void DisplayAnimator::playRandom(bool _loop, int _fps) {
    if (!currentSet || currentSet->count == 0) {
        ESP_LOGW(TAG, "playRandom called but no animation set");
        return;
    }
    const RLEAnimation* a = pickRandom();
    if (!a) return;

    setAnimation(a);
    play(_loop, _fps);
}

void DisplayAnimator::playIndex(int idx, bool _loop, int _fps) {
    if (!currentSet || idx < 0 || idx >= (int)currentSet->count) {
        ESP_LOGW(TAG, "playIndex invalid idx %d", idx);
        return;
    }
    const RLEAnimation* a = currentSet->list[idx];

    setAnimation(a);
    play(_loop, _fps);
}

void DisplayAnimator::stop() {
    running = false;
    frameIndex = 0;
}

int DisplayAnimator::frameCount() const {
    if (currentAnim) return currentAnim->num_frames;
    return 0;   // set → variable length
}


// -----------------------------------------------------------------------------
// Task entry / loop
// -----------------------------------------------------------------------------
void DisplayAnimator::taskEntry(void* param) {
    DisplayAnimator* self = static_cast<DisplayAnimator*>(param);
    if (self) self->taskLoop();
    vTaskDelete(nullptr);   // kết thúc task
}

void DisplayAnimator::taskLoop() {
    ESP_LOGI(TAG, "DisplayAnimator task started");

    if (!display) {
        ESP_LOGE(TAG, "No display attached, exiting animator task");
        return;
    }

    // fallback nếu currentAnim rỗng và có set
    if (!currentAnim && currentSet && currentSet->count > 0) {
        currentAnim = currentSet->list[0];
    }

    while (true) {
        if (!running || !currentAnim) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        bool ok = drawRLEFrame(frameIndex);
        if (!ok) {
            ESP_LOGW(TAG, "drawRLEFrame failed at frame %d", frameIndex);
        }

        frameIndex++;

        if (frameIndex >= (int)currentAnim->num_frames) {
            if (loop) {
                frameIndex = 0;
                if (currentSet && currentSet->count > 1) {
                    currentAnim = pickRandom();
                }
            } else {
                running = false;
                frameIndex = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000 / fps));
    }
}


// -----------------------------------------------------------------------------
// RLE FRAME RENDER (SCANLINE MODE – OPTIMIZED)
// -----------------------------------------------------------------------------
bool DisplayAnimator::drawRLEFrame(int idx) {
    if (!currentAnim) return false;
    if (idx >= currentAnim->num_frames) return false;

    const RLEFrame& frame = currentAnim->frames[idx];
    if (frame.num_segments == 0) return false;

    const int w = currentAnim->width;
    const int h = currentAnim->height;

    static std::vector<uint16_t> rowBuf;
    if ((int)rowBuf.size() < w) rowBuf.resize(w);

    int row = 0;
    int col = 0;

    for (uint32_t s = 0; s < frame.num_segments; s++) {
        uint16_t run   = frame.segments[s].count;
        uint16_t color = frame.segments[s].color;

        while (run > 0) {
            const int remainingRow = w - col;
            const int take = (run < remainingRow) ? run : remainingRow;

            for (int i = 0; i < take; i++) {
                rowBuf[col + i] = color;
            }

            col += take;
            run -= take;

            if (col >= w) {
                display->drawScanline(row, rowBuf.data(), w);
                col = 0;
                row++;
                if (row >= h) return true;
            }
        }
    }

    // partial last row
    if (col > 0 && row < h) {
        display->drawScanline(row, rowBuf.data(), w);
    }

    return true;
}


// -----------------------------------------------------------------------------
// pickRandom()
// -----------------------------------------------------------------------------
#include "esp_system.h"
const RLEAnimation* DisplayAnimator::pickRandom() const {
    if (!currentSet || currentSet->count == 0) return nullptr;
    int idx = esp_random() % currentSet->count;
    return currentSet->list[idx];
}
