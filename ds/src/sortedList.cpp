#ifndef SORTED_LIST_IMPLEMENTATION_INCLUDED
#define SORTED_LIST_IMPLEMENTATION_INCLUDED

#include "sortedList.h"

template <class Type, class Compare>
sortedList<Type, Compare>::sortedList() = default;

template <class Type, class Compare>
sortedList<Type, Compare>::~sortedList() {
    while (head != nullptr) {
        Node* oldHead = head;
        head = head->next;
        delete oldHead;
    }
    tail = nullptr;
}

template <class Type, class Compare>
void sortedList<Type, Compare>::insertTask(const Type& val) {
    if (head == nullptr) {
        head = new Node{val, nullptr};
        tail = head;
        return;
    }

    // Compare(a, b) is true if a should appear before b.
    if (compare(val, head->data)) {
        head = new Node{val, head};
        return;
    }

    // Equal keys go after existing equal keys, preserving their order.
    Node* current = head;
    while (current->next != nullptr &&
           !compare(val, current->next->data)) {
        current = current->next;
    }

    Node* newNode = new Node{val, current->next};
    current->next = newNode;
    if (newNode->next == nullptr) {
        tail = newNode;
    }
}

template <class Type, class Compare>
bool sortedList<Type, Compare>::getValueAtHead(Type& result) const {
    if (head == nullptr) {
        return false;
    }
    result = head->data;
    return true;
}

template <class Type, class Compare>
bool sortedList<Type, Compare>::isEmpty() const {
    return head == nullptr;
}

template <class Type, class Compare>
bool sortedList<Type, Compare>::removeHead() {
    if (head == nullptr) {
        return false;
    }

    Node* oldHead = head;
    head = head->next;
    delete oldHead;

    if (head == nullptr) {
        tail = nullptr;
    }
    return true;
}

template <class Type, class Compare>
bool sortedList<Type, Compare>::deleteTask(const Type& val) {
    if (head == nullptr) {
        return false;
    }

    if (head->data == val) {
        return removeHead();
    }

    Node* previous = head;
    while (previous->next != nullptr && !(previous->next->data == val)) {
        previous = previous->next;
    }

    if (previous->next == nullptr) {
        return false;
    }

    Node* toDelete = previous->next;
    previous->next = toDelete->next;
    if (toDelete == tail) {
        tail = previous;
    }
    delete toDelete;
    return true;
}

#endif