#ifndef DYNAMIC_ARRAY_H
#define DYNAMIC_ARRAY_H

// A resizable array that stores pointers to tasks or other objects.
// The array manages its own memory but does not delete the stored objects.
class DynamicArray {
private:
    void** elements;  // Array of pointers
    int count;        // Number of stored pointers
    int maxSize;      // Current capacity

    void growIfFull();

public:
    explicit DynamicArray(int initialCapacity = 4);
    DynamicArray(const DynamicArray& other);
    DynamicArray& operator=(const DynamicArray& other);
    DynamicArray(DynamicArray&& other);
    DynamicArray& operator=(DynamicArray&& other);
    ~DynamicArray();

    int size() const;
    int capacity() const;
    bool empty() const;

    void* at(int index) const;
    void set(int index, void* value);

    void pushBack(void* value);              // Add to the end
    void insert(int index, void* value);     // Insert at a position
    void* popBack();                         // Remove the last element
    void* erase(int index);                  // Remove at a position
    bool removeFirst(void* value);           // Remove the first match

    int indexOf(void* value) const;          // Returns size() if not found
    bool contains(void* value) const;

    void reserve(int newCapacity);           // Increase capacity if needed
    void clear();                            // Remove all elements
};

#endif