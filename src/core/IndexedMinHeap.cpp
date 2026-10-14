#include "core/IndexedMinHeap.h"

#include "core/Check.h"

#include <utility>

namespace ubahn::core {
namespace {

const EventKey& absentKey() {
    static const EventKey key;
    return key;
}

bool ownerInRange(const std::vector<int>& pos, int owner) {
    return owner >= 0 && static_cast<std::size_t>(owner) < pos.size();
}

void swapNodes(std::vector<EventKey>& heap, std::vector<int>& pos, int left, int right) {
    std::swap(heap[static_cast<std::size_t>(left)], heap[static_cast<std::size_t>(right)]);
    pos[static_cast<std::size_t>(heap[static_cast<std::size_t>(left)].owner)] = left;
    pos[static_cast<std::size_t>(heap[static_cast<std::size_t>(right)].owner)] = right;
}

} // namespace

IndexedMinHeap::IndexedMinHeap(int owners)
    : pos(static_cast<std::size_t>(owners > 0 ? owners : 0), -1) {}

bool IndexedMinHeap::contains(int owner) const {
    if (!ownerInRange(pos, owner)) {
        return false;
    }
    return pos[static_cast<std::size_t>(owner)] >= 0;
}

void IndexedMinHeap::set(int owner, EventKey key) {
    UBAHN_ASSERT(ownerInRange(pos, owner), "owner in range");
    if (!ownerInRange(pos, owner)) {
        return;
    }
    // pos is indexed by the argument. The stored key uses that same owner (ALGORITHM 4.1).
    key.owner = owner;
    const int index = pos[static_cast<std::size_t>(owner)];
    if (index < 0) {
        heap.push_back(key);
        const int placed = static_cast<int>(heap.size()) - 1;
        pos[static_cast<std::size_t>(owner)] = placed;
        siftUp(placed);
        return;
    }
    const EventKey previous = heap[static_cast<std::size_t>(index)];
    heap[static_cast<std::size_t>(index)] = key;
    if (key < previous) {
        siftUp(index);
    } else if (previous < key) {
        siftDown(index);
    }
}

void IndexedMinHeap::erase(int owner) {
    if (!contains(owner)) {
        return;
    }
    const int index = pos[static_cast<std::size_t>(owner)];
    const int last = static_cast<int>(heap.size()) - 1;
    pos[static_cast<std::size_t>(owner)] = -1;
    if (index == last) {
        heap.pop_back();
        return;
    }
    const EventKey removed = heap[static_cast<std::size_t>(index)];
    heap[static_cast<std::size_t>(index)] = heap[static_cast<std::size_t>(last)];
    pos[static_cast<std::size_t>(heap[static_cast<std::size_t>(index)].owner)] = index;
    heap.pop_back();
    if (heap[static_cast<std::size_t>(index)] < removed) {
        siftUp(index);
    } else {
        siftDown(index);
    }
}

const EventKey& IndexedMinHeap::top() const {
    UBAHN_ASSERT(!heap.empty(), "top on empty heap");
    if (heap.empty()) {
        return absentKey();
    }
    return heap.front();
}

EventKey IndexedMinHeap::pop() {
    UBAHN_ASSERT(!heap.empty(), "pop on empty heap");
    if (heap.empty()) {
        return {};
    }
    const EventKey key = heap.front();
    erase(key.owner);
    return key;
}

int IndexedMinHeap::size() const {
    return static_cast<int>(heap.size());
}

bool IndexedMinHeap::empty() const {
    return heap.empty();
}

const EventKey& IndexedMinHeap::keyOf(int owner) const {
    UBAHN_ASSERT(contains(owner), "keyOf requires contains(owner)");
    if (!contains(owner)) {
        return absentKey();
    }
    return heap[static_cast<std::size_t>(pos[static_cast<std::size_t>(owner)])];
}

void IndexedMinHeap::siftUp(int index) {
    while (index > 0) {
        const int parent = (index - 1) / 2;
        if (!(heap[static_cast<std::size_t>(index)] < heap[static_cast<std::size_t>(parent)])) {
            return;
        }
        swapNodes(heap, pos, index, parent);
        index = parent;
    }
}

void IndexedMinHeap::siftDown(int index) {
    const int count = static_cast<int>(heap.size());
    while (true) {
        const int left = index * 2 + 1;
        if (left >= count) {
            return;
        }
        int smallest = left;
        const int right = left + 1;
        if (right < count &&
            heap[static_cast<std::size_t>(right)] < heap[static_cast<std::size_t>(left)]) {
            smallest = right;
        }
        if (!(heap[static_cast<std::size_t>(smallest)] < heap[static_cast<std::size_t>(index)])) {
            return;
        }
        swapNodes(heap, pos, index, smallest);
        index = smallest;
    }
}

} // namespace ubahn::core
