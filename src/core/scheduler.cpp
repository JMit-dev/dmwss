#include "scheduler.hpp"
#include <spdlog/spdlog.h>

Scheduler::Scheduler()
    : m_current_cycle(0) {
}

void Scheduler::Schedule(EventType type, u64 cycles, EventCallback callback) {
    ScheduledEvent event;
    event.type = type;
    event.fire_at_cycle = m_current_cycle + cycles;
    event.callback = std::move(callback);

    m_event_queue.push(event);

    spdlog::trace("Scheduled event type {} to fire at cycle {}",
                  static_cast<int>(type), event.fire_at_cycle);
}

void Scheduler::Deschedule(EventType type) {
    // Create a new queue without the specified event type
    std::priority_queue<
        ScheduledEvent,
        std::vector<ScheduledEvent>,
        std::greater<ScheduledEvent>
    > new_queue;

    // Copy all events except those of the specified type
    while (!m_event_queue.empty()) {
        auto event = m_event_queue.top();
        m_event_queue.pop();

        if (event.type != type) {
            new_queue.push(event);
        }
    }

    m_event_queue = std::move(new_queue);
    spdlog::trace("Descheduled all events of type {}", static_cast<int>(type));
}

void Scheduler::Advance(u64 cycles) {
    m_current_cycle += cycles;
}

void Scheduler::ProcessEvents() {
    while (!m_event_queue.empty()) {
        const auto& next_event = m_event_queue.top();

        // Check if this event is ready to fire
        if (next_event.fire_at_cycle <= m_current_cycle) {
            spdlog::trace("Processing event type {} at cycle {}",
                          static_cast<int>(next_event.type), m_current_cycle);

            // Execute the callback
            next_event.callback();

            // Remove the event from the queue
            m_event_queue.pop();
        } else {
            // Events are ordered by fire time, so if this one isn't ready,
            // no subsequent events will be ready either
            break;
        }
    }
}

u64 Scheduler::GetCyclesUntilNextEvent() const {
    if (m_event_queue.empty()) {
        // Return a large number if no events are scheduled
        return 0xFFFFFFFFFFFFFFFF;
    }

    const auto& next_event = m_event_queue.top();

    if (next_event.fire_at_cycle <= m_current_cycle) {
        return 0;  // Event is ready now
    }

    return next_event.fire_at_cycle - m_current_cycle;
}

void Scheduler::Reset() {
    // Clear all events
    while (!m_event_queue.empty()) {
        m_event_queue.pop();
    }

    m_current_cycle = 0;
    spdlog::debug("Scheduler reset");
}
