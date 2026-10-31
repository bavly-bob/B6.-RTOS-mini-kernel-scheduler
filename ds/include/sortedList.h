#ifndef SORTED_LIST_H
#define SORTED_LIST_H

#include <functional>

template <class Type, class Compare = std::less<Type>>
class sortedList {
private:
    struct Node {
        Type data;
        Node* next;
    };

    Node* head = nullptr;
    Node* tail = nullptr;
    Compare compare;

public:
    sortedList();
    ~sortedList();

    sortedList(const sortedList&) = delete;
    sortedList& operator=(const sortedList&) = delete;

    void insertTask(const Type& val);
    bool getValueAtHead(Type& result) const;
    bool isEmpty() const;
    bool removeHead();
    bool deleteTask(const Type& val);
};

#include "sortedList.cpp"

#endif