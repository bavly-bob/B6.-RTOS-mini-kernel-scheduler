#ifndef MINHEAP_H
#define MINHEAP_H


struct EmptyHeapError {
    const char* message;
};

template <typename T>
class MinHeap {
public:
    MinHeap();


    MinHeap(const T* values, int count);

    MinHeap(const MinHeap& other);
    MinHeap& operator=(const MinHeap& other);
    ~MinHeap();

    void insert(const T& value);


    const T& peek() const;

    T extractMin();

    bool isEmpty() const;
    int size() const;
    void clear();

private:
    static const int INITIAL_CAPACITY = 8;

    T* data;
    int count;
    int capacity;

    static int parent(int i);
    static int left(int i);
    static int right(int i);
    static void swapValues(T& a, T& b);

    void grow();
    void siftUp(int i);
    void siftDown(int i);
};


#include "MinHeap.cpp"

#endif // MINHEAP_H