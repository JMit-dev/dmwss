#pragma once
#include "../types.hpp"
#include <functional>
#include <queue>
#include <vector>

class Scheduler {
public:
    // Event types that can be scheduled
    enum class EventType {
        VBLANK,
        HBLANK,
        HBLANK_EXIT,
        OAM_SCAN,
        LCD_TRANSFER,
        TIMER_OVERFLOW,
        SERIAL_TRANSFER,
        APU_CHANNEL_1,
        APU_CHANNEL_2,
        APU_CHANNEL_3,
        APU_CHANNEL_4,
        APU_FRAME_SEQUENCER,
        DMA_TRANSFER,
        JOYPAD_INTERRUPT
    };

    // Callback function type
    using EventCallback = std::function<void()>;

    Scheduler();
    ~Scheduler() = default;

    // Schedule an event to fire after 'cycles' cycles from now
    void Schedule(EventType type, u64 cycles, EventCallback callback);

    // Remove all events of a specific type
    void Deschedule(EventType type);

    // Advance the scheduler by 'cycles' cycles
    void Advance(u64 cycles);

    // Process all events that are ready to fire
    void ProcessEvents();

    // Get the current cycle count
    u64 GetCurrentCycle() const { return m_current_cycle; }

    // Get cycles until next event (useful for CPU timing)
    u64 GetCyclesUntilNextEvent() const;

    // Reset the scheduler
    void Reset();

private:
    struct ScheduledEvent {
        EventType type;
        u64 fire_at_cycle;  // Absolute cycle when this event fires
        EventCallback callback;

        // For priority queue ordering (earlier events have higher priority)
        bool operator>(const ScheduledEvent& other) const {
            return fire_at_cycle > other.fire_at_cycle;
        }
    };

    // Priority queue with earliest event at the top
    std::priority_queue<
        ScheduledEvent,
        std::vector<ScheduledEvent>,
        std::greater<ScheduledEvent>
    > m_event_queue;

    u64 m_current_cycle;
};
