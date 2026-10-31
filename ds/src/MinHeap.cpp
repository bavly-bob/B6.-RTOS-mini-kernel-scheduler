#ifndef MINHEAP_CPP
#define MINHEAP_CPP

#include "MinHeap.h"

template <typename T>
MinHeap<T>::MinHeap()
    : data(new T[INITIAL_CAPACITY]), count(0), capacity(INITIAL_CAPACITY) {
}

template <typename T>
MinHeap<T>::MinHeap(const T* values, int count)
    : data(nullptr), count(count), capacity(INITIAL_CAPACITY) {
    if (count > capacity) {
        capacity = count;
    }

    data = new T[capacity];
    for (int i = 0; i < count; ++i) {
        data[i] = values[i];
    }

    // Sift down every non-leaf node, starting from the last one.
    for (int i = count / 2 - 1; i >= 0; --i) {
        siftDown(i);
    }
}

template <typename T>
MinHeap<T>::MinHeap(const MinHeap& other)
    : data(new T[other.capacity]), count(other.count), capacity(other.capacity) {
    for (int i = 0; i < count; ++i) {
        data[i] = other.data[i];
    }
}

template <typename T>
MinHeap<T>& MinHeap<T>::operator=(const MinHeap& other) {
    if (this != &other) {
        T* newData = new T[other.capacity];
        for (int i = 0; i < other.count; ++i) {
            newData[i] = other.data[i];
        }

        delete[] data;
        data = newData;
        count = other.count;
        capacity = other.capacity;
    }
    return *this;
}

template <typename T>
MinHeap<T>::~MinHeap() {
    delete[] data;
}

template <typename T>
void MinHeap<T>::insert(const T& value) {
    if (count == capacity) {
        grow();
    }

    data[count] = value;
    ++count;
    siftUp(count - 1);
}

template <typename T>
const T& MinHeap<T>::peek() const {
    if (count == 0) {
        throw EmptyHeapError{"MinHeap::peek: heap is empty"};
    }
    return data[0];
}

template <typename T>
T MinHeap<T>::extractMin() {
    if (count == 0) {
        throw EmptyHeapError{"MinHeap::extractMin: heap is empty"};
    }

    T minValue = data[0];

    // Move the last element to the root, then restore the heap property.
    --count;
    if (count > 0) {
        data[0] = data[count];
        siftDown(0);
    }

    return minValue;
}

template <typename T>
bool MinHeap<T>::isEmpty() const {
    return count == 0;
}

template <typename T>
int MinHeap<T>::size() const {
    return count;
}

template <typename T>
void MinHeap<T>::clear() {
    count = 0;
}

template <typename T>
int MinHeap<T>::parent(int i) {
    return (i - 1) / 2;
}

template <typename T>
int MinHeap<T>::left(int i) {
    return 2 * i + 1;
}

template <typename T>
int MinHeap<T>::right(int i) {
    return 2 * i + 2;
}

template <typename T>
void MinHeap<T>::swapValues(T& a, T& b) {
    T temp = a;
    a = b;
    b = temp;
}

template <typename T>
void MinHeap<T>::grow() {
    int newCapacity = capacity * 2;
    T* newData = new T[newCapacity];
    for (int i = 0; i < count; ++i) {
        newData[i] = data[i];
    }

    delete[] data;
    data = newData;
    capacity = newCapacity;
}

template <typename T>
void MinHeap<T>::siftUp(int i) {
    while (i > 0 && data[i] < data[parent(i)]) {
        swapValues(data[i], data[parent(i)]);
        i = parent(i);
    }
}

template <typename T>
void MinHeap<T>::siftDown(int i) {
    while (true) {
        int smallest = i;

        if (left(i) < count && data[left(i)] < data[smallest]) {
            smallest = left(i);
        }
        if (right(i) < count && data[right(i)] < data[smallest]) {
            smallest = right(i);
        }
        if (smallest == i) {
            return;
        }

        swapValues(data[i], data[smallest]);
        i = smallest;
    }
}

#endif // MINHEAP_CPP