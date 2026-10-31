#ifndef SORTED_LIST_H
#define SORTED_LIST_H

#include <functional>

template<class Type, class Compare = std::less<Type>>
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
    // Constructor
    sortedList() = default;

    // Destructor
    ~sortedList() {
        while (!isEmpty()) {
            removeHead();
        }
    }

    // Prevent copying
    sortedList(const sortedList&) = delete;
    sortedList& operator=(const sortedList&) = delete;

    // Insert a task in ascending order
    void insertTask(const Type& val) {
        Node* newNode = new Node{val, nullptr};

        // Empty list
        if (isEmpty()) {
            head = tail = newNode;
            return;
        }

        // Insert before head
        if (compare(val, head->data)) {
            newNode->next = head;
            head = newNode;
            return;
        }

        // Find the correct position
        Node* current = head;

        while (current->next != nullptr &&
               !compare(val, current->next->data)) {
            current = current->next;
        }

        // Insert the new node
        newNode->next = current->next;
        current->next = newNode;

        // Update tail if inserted at the end
        if (newNode->next == nullptr) {
            tail = newNode;
        }
    }

    // Retrieve the head's value
    bool getValueAtHead(Type& result) const {
        if (isEmpty()) {
            return false;
        }

        result = head->data;
        return true;
    }

    // Check if the list is empty
    bool isEmpty() const {
        return head == nullptr;
    }

    // Remove the first node
    bool removeHead() {
        if (isEmpty()) {
            return false;
        }

        Node* temp = head;
        head = head->next;

        delete temp;

        // List became empty
        if (head == nullptr) {
            tail = nullptr;
        }

        return true;
    }

    // Delete a specific task
    bool deleteTask(const Type& val) {
        if (isEmpty()) {
            return false;
        }

        // Task is at the head
        if (head->data == val) {
            return removeHead();
        }

        Node* current = head;

        // Find the node before the target
        while (current->next != nullptr &&
               !(current->next->data == val)) {
            current = current->next;
        }

        // Task not found
        if (current->next == nullptr) {
            return false;
        }

        Node* temp = current->next;
        current->next = temp->next;

        // Update tail if necessary
        if (temp == tail) {
            tail = current;
        }

        delete temp;
        return true;
    }
};

#endif