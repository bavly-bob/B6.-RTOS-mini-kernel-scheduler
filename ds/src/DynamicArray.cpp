#include "DynamicArray.h"
#include <climits>
#include <stdexcept>

DynamicArray::DynamicArray(int initialCapacity)
    : elements(nullptr), count(0), maxSize(0) {
    if (initialCapacity < 0) {
        throw std::invalid_argument("Capacity cannot be negative");
    }
    reserve(initialCapacity);
}

DynamicArray::DynamicArray(const DynamicArray& other)
    : DynamicArray(other.maxSize) {
    // Copy the pointers into a new array, not the objects they point to.
    for (int i = 0; i < other.count; i++) {
        elements[i] = other.elements[i];
    }
    count = other.count;
}

DynamicArray& DynamicArray::operator=(const DynamicArray& other) {
    if (this == &other) return *this;

    // Allocate first so the original array stays unchanged if allocation fails.
    void** newElements = other.maxSize > 0 ? new void*[other.maxSize] : nullptr;
    for (int i = 0; i < other.count; i++) {
        newElements[i] = other.elements[i];
    }

    delete[] elements;
    elements = newElements;
    count = other.count;
    maxSize = other.maxSize;
    return *this;
}

DynamicArray::DynamicArray(DynamicArray&& other)
    : elements(other.elements), count(other.count), maxSize(other.maxSize) {
    // Take the existing array instead of allocating and copying it.
    other.elements = nullptr;
    other.count = 0;
    other.maxSize = 0;
}

DynamicArray& DynamicArray::operator=(DynamicArray&& other) {
    if (this == &other) return *this;

    delete[] elements;
    elements = other.elements;
    count = other.count;
    maxSize = other.maxSize;
    other.elements = nullptr;
    other.count = 0;
    other.maxSize = 0;
    return *this;
}

DynamicArray::~DynamicArray() {
    delete[] elements;
}

int DynamicArray::size() const { return count; }
int DynamicArray::capacity() const { return maxSize; }
bool DynamicArray::empty() const { return count == 0; }

void* DynamicArray::at(int index) const {
    if (index < 0 || index >= count) {
        throw std::out_of_range("Invalid index");
    }
    return elements[index];
}

void DynamicArray::set(int index, void* value) {
    if (index < 0 || index >= count) {
        throw std::out_of_range("Invalid index");
    }
    elements[index] = value;
}

void DynamicArray::growIfFull() {
    if (count < maxSize) return;

    // Double the capacity so adding elements is usually O(1).
    if (maxSize > INT_MAX / 2) {
        throw std::length_error("Array too large");
    }
    reserve(maxSize == 0 ? 1 : maxSize * 2);
}

void DynamicArray::pushBack(void* value) {
    growIfFull();
    elements[count] = value;
    count++;
}

void DynamicArray::insert(int index, void* value) {
    if (index < 0 || index > count) {
        throw std::out_of_range("Invalid index");
    }
    growIfFull();

    // Shift elements right to make space at the requested position.
    for (int i = count; i > index; i--) {
        elements[i] = elements[i - 1];
    }
    elements[index] = value;
    count++;
}

void* DynamicArray::popBack() {
    if (empty()) {
        throw std::out_of_range("Array is empty");
    }
    return erase(count - 1);
}

void* DynamicArray::erase(int index) {
    if (index < 0 || index >= count) {
        throw std::out_of_range("Invalid index");
    }

    void* removed = elements[index];
    // Shift the remaining elements left to close the gap.
    for (int i = index; i < count - 1; i++) {
        elements[i] = elements[i + 1];
    }
    count--;
    elements[count] = nullptr;
    return removed;
}

bool DynamicArray::removeFirst(void* value) {
    int index = indexOf(value);
    if (index == count) return false;
    erase(index);
    return true;
}

int DynamicArray::indexOf(void* value) const {
    for (int i = 0; i < count; i++) {
        if (elements[i] == value) return i;
    }
    return count;
}

bool DynamicArray::contains(void* value) const {
    return indexOf(value) != count;
}

void DynamicArray::reserve(int newCapacity) {
    if (newCapacity < 0) {
        throw std::invalid_argument("Capacity cannot be negative");
    }
    if (newCapacity <= maxSize) return;

    void** newElements = new void*[newCapacity];
    for (int i = 0; i < count; i++) {
        newElements[i] = elements[i];
    }
    delete[] elements;
    elements = newElements;
    maxSize = newCapacity;
}

void DynamicArray::clear() {
    // Keep the allocated memory so the array can be reused.
    for (int i = 0; i < count; i++) {
        elements[i] = nullptr;
    }
    count = 0;
}