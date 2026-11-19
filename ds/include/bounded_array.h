#ifndef B6_SCHEDULER_DS_BOUNDED_ARRAY_H
#define B6_SCHEDULER_DS_BOUNDED_ARRAY_H

#include <cstddef>
#include <stdexcept>

// BoundedArray<T>: a fixed-capacity array of values, allocated once.
//
// Purpose in B6: bounded trace storage (events, CPU segments, snapshot rows).
// The run limits give every capacity before simulation starts, so the trace
// allocates once at construction and push() never reallocates mid-run.
//
// Design decisions
// - push() returns false when full instead of growing or throwing. Running out
//   of trace space is an expected, reportable outcome ("trace truncated"), not
//   a program error, so the caller decides how to record it.
// - Values are stored by copy; T must be default-constructible and copy-
//   assignable. The array never holds or frees caller pointers.
// - Copying/moving is disabled: one owner, one allocation.
//
// Costs: construction O(capacity) (default-constructs every slot); push, at,
// back, size O(1); clear O(1) (slots are overwritten by later pushes).
template <typename T>
class BoundedArray {
public:
    explicit BoundedArray(std::size_t capacity)
        : data_(capacity ? new T[capacity] : nullptr), size_(0), capacity_(capacity) {}
    ~BoundedArray() { delete[] data_; }
    BoundedArray(const BoundedArray&) = delete;
    BoundedArray& operator=(const BoundedArray&) = delete;

    // Appends a copy. Returns false, leaving the array unchanged, when full.
    bool push(const T& value) {
        if (size_ == capacity_) return false;
        data_[size_] = value;
        ++size_;
        return true;
    }

    // Bounds-checked access; throws std::out_of_range for index >= size().
    T& at(std::size_t index) {
        if (index >= size_) throw std::out_of_range("BoundedArray index out of range");
        return data_[index];
    }
    const T& at(std::size_t index) const {
        if (index >= size_) throw std::out_of_range("BoundedArray index out of range");
        return data_[index];
    }

    // Last element; throws std::out_of_range when empty.
    T& back() {
        if (size_ == 0) throw std::out_of_range("BoundedArray is empty");
        return data_[size_ - 1];
    }
    const T& back() const {
        if (size_ == 0) throw std::out_of_range("BoundedArray is empty");
        return data_[size_ - 1];
    }

    std::size_t size() const noexcept { return size_; }
    std::size_t capacity() const noexcept { return capacity_; }
    bool isEmpty() const noexcept { return size_ == 0; }
    bool isFull() const noexcept { return size_ == capacity_; }
    void clear() noexcept { size_ = 0; }

private:
    T* data_;              // capacity_ slots, owned; nullptr when capacity is 0.
    std::size_t size_;     // Slots [0, size_) hold pushed values.
    std::size_t capacity_; // Fixed at construction.
};

#endif  // B6_SCHEDULER_DS_BOUNDED_ARRAY_H
