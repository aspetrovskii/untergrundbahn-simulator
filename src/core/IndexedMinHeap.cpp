#include "core/IndexedMinHeap.h"

namespace ubahn::core {
namespace {

const EventKey& absentKey() {
    static const EventKey key;
    return key;
}

} // namespace

IndexedMinHeap::IndexedMinHeap(int owners)
    : pos(static_cast<std::size_t>(owners > 0 ? owners : 0), -1) {}

bool IndexedMinHeap::contains(int) const {
    // TODO(BE-02)
    return false;
}

void IndexedMinHeap::set(int, EventKey) {
    // TODO(BE-02)
}

void IndexedMinHeap::erase(int) {
    // TODO(BE-02)
}

const EventKey& IndexedMinHeap::top() const {
    // TODO(BE-02)
    return absentKey();
}

EventKey IndexedMinHeap::pop() {
    // TODO(BE-02)
    return {};
}

int IndexedMinHeap::size() const {
    // TODO(BE-02)
    return 0;
}

bool IndexedMinHeap::empty() const {
    // TODO(BE-02)
    return true;
}

const EventKey& IndexedMinHeap::keyOf(int) const {
    // TODO(BE-02)
    return absentKey();
}

void IndexedMinHeap::siftUp(int) {
    // TODO(BE-02)
}

void IndexedMinHeap::siftDown(int) {
    // TODO(BE-02)
}

} // namespace ubahn::core
