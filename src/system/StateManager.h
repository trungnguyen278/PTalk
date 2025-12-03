#pragma once
#include <functional>
#include <vector>
#include <mutex>
#include "StateTypes.h"

class StateManager {
public:
    using InteractionCb = std::function<void(state::InteractionState)>;
    using ConnectivityCb = std::function<void(state::ConnectivityState)>;
    using SystemCb = std::function<void(state::SystemState)>;

    static StateManager& instance();

    // Setters (notify subscribers if changed)
    void setInteractionState(state::InteractionState s);
    void setConnectivityState(state::ConnectivityState s);
    void setSystemState(state::SystemState s);

    // Getters
    state::InteractionState getInteractionState();
    state::ConnectivityState getConnectivityState();
    state::SystemState getSystemState();

    // Subscribe (returns handle id) — simple pattern
    int subscribeInteraction(InteractionCb cb);
    int subscribeConnectivity(ConnectivityCb cb);
    int subscribeSystem(SystemCb cb);

    // Unsubscribe
    void unsubscribeInteraction(int id);
    void unsubscribeConnectivity(int id);
    void unsubscribeSystem(int id);

private:
    StateManager() = default;
    StateManager(const StateManager&) = delete;
    StateManager& operator=(const StateManager&) = delete;

    // internals
    std::mutex mtx;

    state::InteractionState interaction_state = state::InteractionState::IDLE;
    state::ConnectivityState connectivity_state = state::ConnectivityState::OFFLINE;
    state::SystemState system_state = state::SystemState::BOOTING;

    // simple subscription containers
    int next_sub_id = 1;
    std::vector<std::pair<int, InteractionCb>> interaction_cbs;
    std::vector<std::pair<int, ConnectivityCb>> connectivity_cbs;
    std::vector<std::pair<int, SystemCb>> system_cbs;

    // notify helpers
    void notifyInteraction(state::InteractionState s);
    void notifyConnectivity(state::ConnectivityState s);
    void notifySystem(state::SystemState s);
};
