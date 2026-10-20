#pragma once

#include "api/Types.h"

#include <cstdint>
#include <vector>

namespace ubahn::core {

// Lexicographic key (time, priority, owner). ALGORITHM 4.1.
struct EventKey {
    api::SimTime time = 0;
    uint8_t priority = 0;
    int32_t owner = 0;

    bool operator==(const EventKey&) const = default;

    friend bool operator<(const EventKey& lhs, const EventKey& rhs) {
        if (lhs.time != rhs.time) {
            return lhs.time < rhs.time;
        }
        if (lhs.priority != rhs.priority) {
            return lhs.priority < rhs.priority;
        }
        return lhs.owner < rhs.owner;
    }
};

// At most one entry per owner. The heap stores who to wake, not the event type.
class IndexedMinHeap {
public:
    explicit IndexedMinHeap(int owners);

    bool contains(int owner) const;
    void set(int owner, EventKey key);
    void erase(int owner);
    // Precondition: !empty().
    const EventKey& top() const;
    EventKey pop();
    int size() const;
    bool empty() const;
    // Precondition: contains(owner).
    const EventKey& keyOf(int owner) const;

private:
    std::vector<EventKey> heap;
    std::vector<int> pos; // pos[owner] is the heap index, or -1
    void siftUp(int index);
    void siftDown(int index);
};

} // namespace ubahn::core
