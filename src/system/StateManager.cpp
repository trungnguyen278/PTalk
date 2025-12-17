#include "StateManager.hpp"
#include "esp_log.h"
#include <algorithm>

static const char *TAG = "StateManager";

StateManager& StateManager::instance() {
    static StateManager inst;
    return inst;
}

// ===========================================================
// Interaction State + Source
// ===========================================================

void StateManager::setInteractionState(state::InteractionState s, state::InputSource src) {
    std::vector<std::pair<int, InteractionCb>> callbacks;
    
    {
        std::lock_guard<std::mutex> lk(mtx);

        if (s == interaction_state && src == interaction_source) {
            return;  // No change
        }

        interaction_state = s;
        interaction_source = src;

        ESP_LOGI(TAG, "InteractionState: %d -> %d (source=%d)",
            static_cast<int>(interaction_state), static_cast<int>(s), static_cast<int>(src));

        // Copy callbacks under lock
        callbacks = interaction_cbs;
    }  // Lock released here

    // ✅ Call callbacks OUTSIDE lock to prevent deadlocks and timer overflow
    for (auto &p : callbacks) {
        if (p.second) p.second(s, src);
    }
}

state::InteractionState StateManager::getInteractionState() {
    std::lock_guard<std::mutex> lk(mtx);
    return interaction_state;
}

state::InputSource StateManager::getInteractionSource() {
    std::lock_guard<std::mutex> lk(mtx);
    return interaction_source;
}

int StateManager::subscribeInteraction(InteractionCb cb) {
    std::lock_guard<std::mutex> lk(mtx);
    int id = next_sub_id++;
    interaction_cbs.emplace_back(id, std::move(cb));
    return id;
}

void StateManager::unsubscribeInteraction(int id) {
    std::lock_guard<std::mutex> lk(mtx);
    interaction_cbs.erase(
        std::remove_if(interaction_cbs.begin(), interaction_cbs.end(),
        [id](auto &p){ return p.first == id; }),
        interaction_cbs.end()
    );
}

// ===========================================================
// Connectivity
// ===========================================================

void StateManager::setConnectivityState(state::ConnectivityState s) {
    std::vector<std::pair<int, ConnectivityCb>> callbacks;
    
    {
        std::lock_guard<std::mutex> lk(mtx);
        if (s == connectivity_state) return;

        connectivity_state = s;
        ESP_LOGI(TAG, "ConnectivityState: %d (change)", static_cast<int>(s));

        // Copy callbacks under lock
        callbacks = connectivity_cbs;
    }  // Lock released here

    // ✅ Call callbacks OUTSIDE lock to prevent deadlocks
    for (auto &p : callbacks) {
        if (p.second) p.second(s);
    }
}

state::ConnectivityState StateManager::getConnectivityState() {
    std::lock_guard<std::mutex> lk(mtx);
    return connectivity_state;
}

int StateManager::subscribeConnectivity(ConnectivityCb cb) {
    std::lock_guard<std::mutex> lk(mtx);
    int id = next_sub_id++;
    connectivity_cbs.emplace_back(id, std::move(cb));
    return id;
}

void StateManager::unsubscribeConnectivity(int id) {
    std::lock_guard<std::mutex> lk(mtx);
    connectivity_cbs.erase(
        std::remove_if(connectivity_cbs.begin(), connectivity_cbs.end(),
        [id](auto &p){ return p.first == id; }),
        connectivity_cbs.end()
    );
}

// ===========================================================
// System
// ===========================================================

void StateManager::setSystemState(state::SystemState s) {
    std::vector<std::pair<int, SystemCb>> callbacks;
    
    {
        std::lock_guard<std::mutex> lk(mtx);
        if (s == system_state) return;

        system_state = s;
        ESP_LOGI(TAG, "SystemState: %d (change)", static_cast<int>(s));

        // Copy callbacks under lock
        callbacks = system_cbs;
    }  // Lock released here

    // ✅ Call callbacks OUTSIDE lock to prevent deadlocks
    for (auto &p : callbacks) {
        if (p.second) p.second(s);
    }
}

state::SystemState StateManager::getSystemState() {
    std::lock_guard<std::mutex> lk(mtx);
    return system_state;
}

int StateManager::subscribeSystem(SystemCb cb) {
    std::lock_guard<std::mutex> lk(mtx);
    int id = next_sub_id++;
    system_cbs.emplace_back(id, std::move(cb));
    return id;
}

void StateManager::unsubscribeSystem(int id) {
    std::lock_guard<std::mutex> lk(mtx);
    system_cbs.erase(
        std::remove_if(system_cbs.begin(), system_cbs.end(),
        [id](auto &p){ return p.first == id; }),
        system_cbs.end()
    );
}

// ===========================================================
// Power
// ===========================================================

void StateManager::setPowerState(state::PowerState s) {
    std::vector<std::pair<int, PowerCb>> callbacks;
    
    {
        std::lock_guard<std::mutex> lk(mtx);
        if (s == power_state) return;

        power_state = s;
        ESP_LOGI(TAG, "PowerState: %d (change)", static_cast<int>(s));

        // Copy callbacks under lock
        callbacks = power_cbs;
    }  // Lock released here

    // ✅ Call callbacks OUTSIDE lock to prevent deadlocks and timer stack overflow
    for (auto &p : callbacks) {
        if (p.second) p.second(s);
    }
}

state::PowerState StateManager::getPowerState() {
    std::lock_guard<std::mutex> lk(mtx);
    return power_state;
}

int StateManager::subscribePower(PowerCb cb) {
    std::lock_guard<std::mutex> lk(mtx);
    int id = next_sub_id++;
    power_cbs.emplace_back(id, std::move(cb));
    return id;
}

void StateManager::unsubscribePower(int id) {
    std::lock_guard<std::mutex> lk(mtx);
    power_cbs.erase(
        std::remove_if(power_cbs.begin(), power_cbs.end(),
        [id](auto &p){ return p.first == id; }),
        power_cbs.end()
    );
}
