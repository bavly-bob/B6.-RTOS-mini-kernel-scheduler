// Template definitions for sortedList. Included only from ds/include/sortedList.h.
#ifndef SORTED_LIST_IMPLEMENTATION_INCLUDED
#define SORTED_LIST_IMPLEMENTATION_INCLUDED

template <class Type, class Compare>
sortedList<Type, Compare>::sortedList() = default;

template <class Type, class Compare>
sortedList<Type, Compare>::~sortedList() {
    clear();
}

template <class Type, class Compare>
void sortedList<Type, Compare>::clear() {
    // Walk from the head, deleting each node after saving its successor.
    while (head != nullptr) {
        Node* oldHead = head;
        head = head->next;
        delete oldHead;
    }
    tail = nullptr;
    count = 0;
}

template <class Type, class Compare>
void sortedList<Type, Compare>::insertTask(const Type& val) {
    // Case 1: empty list. The new node is both head and tail.
    if (head == nullptr) {
        head = new Node{val, nullptr};
        tail = head;
        ++count;
        return;
    }

    // Case 2: strictly smaller than the head, so it becomes the new head.
    // (An equal key does NOT go first; it must queue behind the existing one.)
    if (compare(val, head->data)) {
        head = new Node{val, head};
        ++count;
        return;
    }

    // Case 3: O(1) shortcut for the common "latest wake time" insert. If val is
    // not smaller than the tail, it belongs after every node, including equals.
    if (!compare(val, tail->data)) {
        tail->next = new Node{val, nullptr};
        tail = tail->next;
        ++count;
        return;
    }

    // Case 4: somewhere in the middle. Stop at the last node that val does
    // not precede; equal keys are skipped so FIFO order is preserved.
    Node* current = head;
    while (current->next != nullptr && !compare(val, current->next->data)) {
        current = current->next;
    }

    // current->next is non-null here (case 3 handled the tail), so the tail
    // pointer never changes in this branch.
    current->next = new Node{val, current->next};
    ++count;
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
std::size_t sortedList<Type, Compare>::size() const {
    return count;
}

template <class Type, class Compare>
bool sortedList<Type, Compare>::removeHead() {
    if (head == nullptr) {
        return false;
    }

    Node* oldHead = head;
    head = head->next;
    delete oldHead;
    --count;

    if (head == nullptr) {
        tail = nullptr;  // The list became empty.
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

    // Keep a pointer to the node BEFORE the match, because a singly linked
    // list can only unlink a node through its predecessor.
    Node* previous = head;
    while (previous->next != nullptr && !(previous->next->data == val)) {
        previous = previous->next;
    }

    if (previous->next == nullptr) {
        return false;  // Not found.
    }

    Node* toDelete = previous->next;
    previous->next = toDelete->next;
    if (toDelete == tail) {
        tail = previous;  // Removed the last node.
    }
    delete toDelete;
    --count;
    return true;
}

#endif
