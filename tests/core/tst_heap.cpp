#include "core/IndexedMinHeap.h"
#include "core/Rng.h"

#include <QtTest>
#include <algorithm>
#include <optional>
#include <vector>

namespace {

ubahn::core::EventKey key(int64_t time, int priority, int owner) {
    return ubahn::core::EventKey{time, static_cast<uint8_t>(priority), owner};
}

} // namespace

class HeapTest : public QObject {
    Q_OBJECT

private slots:
    void emptyHeap();
    void popOrder();
    void tiesByPriorityThenOwner();
    void setIncreaseAndDecrease();
    void eraseTopMiddleAndLast();
    void randomAgainstModel();
};

void HeapTest::emptyHeap() {
    ubahn::core::IndexedMinHeap heap(4);
    QVERIFY(heap.empty());
    QCOMPARE(heap.size(), 0);
    for (int owner = 0; owner < 4; ++owner) {
        QVERIFY(!heap.contains(owner));
    }
    QVERIFY(!heap.contains(-1));
    QVERIFY(!heap.contains(4));
}

void HeapTest::popOrder() {
    ubahn::core::IndexedMinHeap heap(4);
    heap.set(2, key(30, 1, 2));
    heap.set(0, key(10, 1, 0));
    heap.set(3, key(40, 1, 3));
    heap.set(1, key(20, 1, 1));

    QCOMPARE(heap.size(), 4);
    QVERIFY(heap.contains(2));
    QCOMPARE(heap.keyOf(2).time, qint64{30});
    QCOMPARE(static_cast<int>(heap.keyOf(2).priority), 1);
    QCOMPARE(heap.keyOf(2).owner, 2);
    QCOMPARE(heap.top().owner, 0);

    const int expected[] = {0, 1, 2, 3};
    for (int owner : expected) {
        QCOMPARE(heap.pop().owner, owner);
    }
    QVERIFY(heap.empty());
    QCOMPARE(heap.size(), 0);
    QVERIFY(!heap.contains(0));
}

void HeapTest::tiesByPriorityThenOwner() {
    ubahn::core::IndexedMinHeap heap(6);
    heap.set(2, key(100, 1, 2));
    heap.set(0, key(100, 0, 0));
    heap.set(4, key(50, 3, 4));
    heap.set(1, key(100, 0, 1));
    heap.set(3, key(100, 2, 3));
    heap.set(5, key(50, 0, 5));

    // time, then priority, then owner (ALGORITHM 4.2).
    const int expected[] = {5, 4, 0, 1, 2, 3};
    for (int owner : expected) {
        QCOMPARE(heap.pop().owner, owner);
    }
    QVERIFY(heap.empty());
}

void HeapTest::setIncreaseAndDecrease() {
    ubahn::core::IndexedMinHeap heap(5);
    for (int owner = 0; owner < 5; ++owner) {
        heap.set(owner, key(10 * (owner + 1), 1, owner));
    }
    QCOMPARE(heap.top().owner, 0);

    heap.set(0, key(100, 1, 0));
    QCOMPARE(heap.top().owner, 1);
    QCOMPARE(heap.keyOf(0).time, qint64{100});
    QVERIFY(heap.contains(0));

    heap.set(4, key(5, 1, 4));
    QCOMPARE(heap.top().owner, 4);
    QCOMPARE(heap.keyOf(4).time, qint64{5});

    const int expected[] = {4, 1, 2, 3, 0};
    for (int owner : expected) {
        QCOMPARE(heap.pop().owner, owner);
    }
    QVERIFY(heap.empty());
}

void HeapTest::eraseTopMiddleAndLast() {
    {
        ubahn::core::IndexedMinHeap heap(7);
        for (int owner = 0; owner < 7; ++owner) {
            heap.set(owner, key(owner * 10, 1, owner));
        }
        heap.erase(0);
        QVERIFY(!heap.contains(0));
        QCOMPARE(heap.size(), 6);
        const int expected[] = {1, 2, 3, 4, 5, 6};
        for (int owner : expected) {
            QCOMPARE(heap.pop().owner, owner);
        }
    }
    {
        ubahn::core::IndexedMinHeap heap(7);
        for (int owner = 0; owner < 7; ++owner) {
            heap.set(owner, key(owner * 10, 1, owner));
        }
        heap.erase(3);
        QVERIFY(!heap.contains(3));
        QCOMPARE(heap.size(), 6);
        QCOMPARE(heap.keyOf(4).time, qint64{40});
        const int expected[] = {0, 1, 2, 4, 5, 6};
        for (int owner : expected) {
            QCOMPARE(heap.pop().owner, owner);
        }
    }
    {
        ubahn::core::IndexedMinHeap heap(7);
        for (int owner = 0; owner < 7; ++owner) {
            heap.set(owner, key(owner * 10, 1, owner));
        }
        heap.erase(6);
        heap.erase(6);
        QVERIFY(!heap.contains(6));
        QCOMPARE(heap.size(), 6);
        const int expected[] = {0, 1, 2, 3, 4, 5};
        for (int owner : expected) {
            QCOMPARE(heap.pop().owner, owner);
        }
    }
}

void HeapTest::randomAgainstModel() {
    constexpr int kOwners = 17;
    constexpr int kOps = 10000;
    ubahn::core::Rng rng(42);
    ubahn::core::IndexedMinHeap heap(kOwners);
    std::vector<std::optional<ubahn::core::EventKey>> model(static_cast<std::size_t>(kOwners));

    for (int op = 0; op < kOps; ++op) {
        const int owner = rng.uniformInt(0, kOwners - 1);
        const int action = rng.uniformInt(0, 2);
        if (action == 0 || !model[static_cast<std::size_t>(owner)].has_value()) {
            const ubahn::core::EventKey next =
                key(rng.uniformInt(0, 50), rng.uniformInt(0, 3), owner);
            heap.set(owner, next);
            model[static_cast<std::size_t>(owner)] = next;
        } else {
            heap.erase(owner);
            model[static_cast<std::size_t>(owner)].reset();
        }

        int present = 0;
        bool any = false;
        ubahn::core::EventKey best{};
        for (int id = 0; id < kOwners; ++id) {
            const std::optional<ubahn::core::EventKey>& entry = model[static_cast<std::size_t>(id)];
            const bool has = entry.has_value();
            QCOMPARE(heap.contains(id), has);
            if (!has) {
                continue;
            }
            QVERIFY(heap.keyOf(id) == *entry);
            ++present;
            if (!any || *entry < best) {
                best = *entry;
                any = true;
            }
        }
        QCOMPARE(heap.size(), present);
        QCOMPARE(heap.empty(), !any);
        if (any) {
            QVERIFY(heap.top() == best);
        }
    }

    std::vector<ubahn::core::EventKey> present;
    for (const std::optional<ubahn::core::EventKey>& entry : model) {
        if (entry.has_value()) {
            present.push_back(*entry);
        }
    }
    std::sort(present.begin(), present.end());
    for (const ubahn::core::EventKey& expected : present) {
        QVERIFY(heap.pop() == expected);
    }
    QVERIFY(heap.empty());
}

QTEST_APPLESS_MAIN(HeapTest)

#include "tst_heap.moc"
