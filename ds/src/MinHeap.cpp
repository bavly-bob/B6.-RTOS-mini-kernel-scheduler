// Template implementation for MinHeap. Included by ds/include/MinHeap.h;
// do not add this file to a build target's source list.
#ifndef MINHEAP_CPP
#define MINHEAP_CPP

#include "../include/MinHeap.h"

#define MINHEAP_TEMPLATE template <typename T, typename Compare, typename IndexHook>
#define MINHEAP MinHeap<T, Compare, IndexHook>

MINHEAP_TEMPLATE
MINHEAP::MinHeap(const Compare& compare, const IndexHook& indexHook)
    : data(new T[INITIAL_CAPACITY]), count(0), cap(INITIAL_CAPACITY),
      less(compare), hook(indexHook) {
}

MINHEAP_TEMPLATE
MINHEAP::MinHeap(const T* values, int valueCount, const Compare& compare,
                 const IndexHook& indexHook)
    : data(nullptr), count(valueCount > 0 ? valueCount : 0), cap(INITIAL_CAPACITY),
      less(compare), hook(indexHook) {
    if (count > cap) {
        cap = count;
    }

    data = new T[cap];
    for (int i = 0; i < count; ++i) {
        data[i] = values[i];
    }

    // Sift down every non-leaf node, starting from the last one. Positions are
    // reported once at the end instead of on every swap.
    for (int i = count / 2 - 1; i >= 0; --i) {
        int node = i;
        while (true) {
            int smallest = node;
            if (left(node) < count && less(data[left(node)], data[smallest])) smallest = left(node);
            if (right(node) < count && less(data[right(node)], data[smallest])) smallest = right(node);
            if (smallest == node) break;
            T temp = data[node];
            data[node] = data[smallest];
            data[smallest] = temp;
            node = smallest;
        }
    }
    for (int i = 0; i < count; ++i) {
        hook(data[i], i);
    }
}

MINHEAP_TEMPLATE
MINHEAP::MinHeap(const MinHeap& other)
    : data(new T[other.cap]), count(other.count), cap(other.cap),
      less(other.less), hook(other.hook) {
    for (int i = 0; i < count; ++i) {
        data[i] = other.data[i];
    }
}

MINHEAP_TEMPLATE
MINHEAP& MINHEAP::operator=(const MinHeap& other) {
    if (this != &other) {
        T* newData = new T[other.cap];
        for (int i = 0; i < other.count; ++i) {
            newData[i] = other.data[i];
        }

        delete[] data;
        data = newData;
        count = other.count;
        cap = other.cap;
        less = other.less;
        hook = other.hook;
    }
    return *this;
}

MINHEAP_TEMPLATE
MINHEAP::~MinHeap() {
    delete[] data;
}

MINHEAP_TEMPLATE
void MINHEAP::insert(const T& value) {
    if (count == cap) {
        grow();
    }

    data[count] = value;
    hook(data[count], count);
    ++count;
    siftUp(count - 1);
}

MINHEAP_TEMPLATE
const T& MINHEAP::peek() const {
    if (count == 0) {
        throw EmptyHeapError{"MinHeap::peek: heap is empty"};
    }
    return data[0];
}

MINHEAP_TEMPLATE
T MINHEAP::extractMin() {
    if (count == 0) {
        throw EmptyHeapError{"MinHeap::extractMin: heap is empty"};
    }

    T minValue = data[0];
    removeAt(0);
    return minValue;
}

MINHEAP_TEMPLATE
bool MINHEAP::tryPeek(T& out) const {
    if (count == 0) {
        return false;
    }
    out = data[0];
    return true;
}

MINHEAP_TEMPLATE
bool MINHEAP::tryExtractMin(T& out) {
    if (count == 0) {
        return false;
    }
    out = data[0];
    removeAt(0);
    return true;
}

MINHEAP_TEMPLATE
bool MINHEAP::remove(const T& value) {
    for (int i = 0; i < count; ++i) {
        if (data[i] == value) {
            return removeAt(i);
        }
    }
    return false;
}

MINHEAP_TEMPLATE
bool MINHEAP::removeAt(int index) {
    if (index < 0 || index >= count) {
        return false;
    }

    hook(data[index], -1);
    --count;
    if (index != count) {
        // The moved last entry may belong above or below the hole.
        data[index] = data[count];
        hook(data[index], index);
        if (index > 0 && less(data[index], data[parent(index)])) {
            siftUp(index);
        } else {
            siftDown(index);
        }
    }
    return true;
}

MINHEAP_TEMPLATE
const T& MINHEAP::at(int index) const {
    return data[index];
}

MINHEAP_TEMPLATE
void MINHEAP::reserve(int requestedCapacity) {
    if (requestedCapacity > cap) {
        reallocate(requestedCapacity);
    }
}

MINHEAP_TEMPLATE
bool MINHEAP::isEmpty() const {
    return count == 0;
}

MINHEAP_TEMPLATE
int MINHEAP::size() const {
    return count;
}

MINHEAP_TEMPLATE
int MINHEAP::capacity() const {
    return cap;
}

MINHEAP_TEMPLATE
void MINHEAP::clear() {
    for (int i = 0; i < count; ++i) {
        hook(data[i], -1);
    }
    count = 0;
}

MINHEAP_TEMPLATE
int MINHEAP::parent(int i) {
    return (i - 1) / 2;
}

MINHEAP_TEMPLATE
int MINHEAP::left(int i) {
    return 2 * i + 1;
}

MINHEAP_TEMPLATE
int MINHEAP::right(int i) {
    return 2 * i + 2;
}

MINHEAP_TEMPLATE
void MINHEAP::swapSlots(int a, int b) {
    T temp = data[a];
    data[a] = data[b];
    data[b] = temp;
    hook(data[a], a);
    hook(data[b], b);
}

MINHEAP_TEMPLATE
void MINHEAP::reallocate(int newCapacity) {
    // Slot indices are unchanged, so the hook is not called.
    T* newData = new T[newCapacity];
    for (int i = 0; i < count; ++i) {
        newData[i] = data[i];
    }

    delete[] data;
    data = newData;
    cap = newCapacity;
}

MINHEAP_TEMPLATE
void MINHEAP::grow() {
    reallocate(cap * 2);
}

MINHEAP_TEMPLATE
void MINHEAP::siftUp(int i) {
    while (i > 0 && less(data[i], data[parent(i)])) {
        swapSlots(i, parent(i));
        i = parent(i);
    }
}

MINHEAP_TEMPLATE
void MINHEAP::siftDown(int i) {
    while (true) {
        int smallest = i;

        if (left(i) < count && less(data[left(i)], data[smallest])) {
            smallest = left(i);
        }
        if (right(i) < count && less(data[right(i)], data[smallest])) {
            smallest = right(i);
        }
        if (smallest == i) {
            return;
        }

        swapSlots(i, smallest);
        i = smallest;
    }
}

#undef MINHEAP
#undef MINHEAP_TEMPLATE

#endif // MINHEAP_CPP
