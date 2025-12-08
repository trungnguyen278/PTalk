#include "StateManager.hpp"
#include "esp_log.h"
#include <algorithm>

static const char *TAG = "StateManager";

StateManager& StateManager::instance() {
    static StateManager inst;
    return inst;
}

void StateManager::setInteractionState(state::InteractionState s) {
    std::lock_guard<std::mutex> lk(mtx);
    if (s == interaction_state) return;
    interaction_state = s;
    ESP_LOGI(TAG, "InteractionState -> %d", static_cast<int>(s));
    notifyInteraction(s);
}

void StateManager::setConnectivityState(state::ConnectivityState s) {
    std::lock_guard<std::mutex> lk(mtx);
    if (s == connectivity_state) return;
    connectivity_state = s;
    ESP_LOGI(TAG, "ConnectivityState -> %d", static_cast<int>(s));
    notifyConnectivity(s);
}

void StateManager::setSystemState(state::SystemState s) {
    std::lock_guard<std::mutex> lk(mtx);
    if (s == system_state) return;
    system_state = s;
    ESP_LOGI(TAG, "SystemState -> %d", static_cast<int>(s));
    notifySystem(s);
}
void StateManager::setPowerState(state::PowerState s) {
    std::lock_guard<std::mutex> lk(mtx);
    if (s == power_state) return;
    power_state = s;
    ESP_LOGI(TAG, "PowerState -> %d", static_cast<int>(s));
    // No notify for power state for now
}

state::InteractionState StateManager::getInteractionState() {
    std::lock_guard<std::mutex> lk(mtx);
    return interaction_state;
}
state::ConnectivityState StateManager::getConnectivityState() {
    std::lock_guard<std::mutex> lk(mtx);
    return connectivity_state;
}
state::SystemState StateManager::getSystemState() {
    std::lock_guard<std::mutex> lk(mtx);
    return system_state;
}
state::PowerState StateManager::getPowerState() {
    std::lock_guard<std::mutex> lk(mtx);
    return power_state;
}

int StateManager::subscribeInteraction(InteractionCb cb) {
    std::lock_guard<std::mutex> lk(mtx);
    int id = next_sub_id++;
    interaction_cbs.emplace_back(id, std::move(cb));
    return id;
}
int StateManager::subscribeConnectivity(ConnectivityCb cb) {
    std::lock_guard<std::mutex> lk(mtx);
    int id = next_sub_id++;
    connectivity_cbs.emplace_back(id, std::move(cb));
    return id;
}
int StateManager::subscribeSystem(SystemCb cb) {
    std::lock_guard<std::mutex> lk(mtx);
    int id = next_sub_id++;
    system_cbs.emplace_back(id, std::move(cb));
    return id;
}
int StateManager::subscribePower(PowerCb cb) {
    std::lock_guard<std::mutex> lk(mtx);
    int id = next_sub_id++;
    power_cbs.emplace_back(id, std::move(cb));
    return id;
}

void StateManager::unsubscribeInteraction(int id) {
    std::lock_guard<std::mutex> lk(mtx);
    interaction_cbs.erase(std::remove_if(interaction_cbs.begin(), interaction_cbs.end(),
        [id](auto &p){ return p.first == id; }), interaction_cbs.end());
}
void StateManager::unsubscribeConnectivity(int id) {
    std::lock_guard<std::mutex> lk(mtx);
    connectivity_cbs.erase(std::remove_if(connectivity_cbs.begin(), connectivity_cbs.end(),
        [id](auto &p){ return p.first == id; }), connectivity_cbs.end());
}
void StateManager::unsubscribeSystem(int id) {
    std::lock_guard<std::mutex> lk(mtx);
    system_cbs.erase(std::remove_if(system_cbs.begin(), system_cbs.end(),
        [id](auto &p){ return p.first == id; }), system_cbs.end());
}
void StateManager::unsubscribePower(int id) {
    std::lock_guard<std::mutex> lk(mtx);
    power_cbs.erase(std::remove_if(power_cbs.begin(), power_cbs.end(),
        [id](auto &p){ return p.first == id; }), power_cbs.end());
}

void StateManager::notifyInteraction(state::InteractionState s) {
    // copy callbacks to call outside lock if needed
    auto tmp = interaction_cbs;
    for (auto &p : tmp) {
        if (p.second) p.second(s);
    }
}
void StateManager::notifyConnectivity(state::ConnectivityState s) {
    auto tmp = connectivity_cbs;
    for (auto &p : tmp) {
        if (p.second) p.second(s);
    }
}
void StateManager::notifySystem(state::SystemState s) {
    auto tmp = system_cbs;
    for (auto &p : tmp) {
        if (p.second) p.second(s);
    }
}
void StateManager::notifyPower(state::PowerState s) {
    auto tmp = power_cbs;
    for (auto &p : tmp) {
        if (p.second) p.second(s);
    }
}
