#include "../include/circular_list.h"


template <typename T>
T* CircularList<T>::Node::getValue() const noexcept { return value_; }

template <typename T>
typename CircularList<T>::Node* CircularList<T>::Node::getNext() const noexcept { return next_; }

template <typename T>
typename CircularList<T>::Node* CircularList<T>::Node::getPrevious() const noexcept { return previous_; }

template <typename T>
CircularList<T>::Node::Node(T* value, const CircularList* owner) noexcept
    : value_(value), next_(this), previous_(this), owner_(owner) {}

template <typename T>
CircularList<T>::CircularList() noexcept : head_(nullptr), size_(0) {}

template <typename T>
CircularList<T>::~CircularList() {
    while (!isEmpty()) {
        popFront();
    }
}

template <typename T>
typename CircularList<T>::Node* CircularList<T>::pushBack(T* value) {
    if (value == nullptr) {
        return nullptr;
    }

    Node* node = new Node(value, this);
    if (head_ == nullptr) {
        head_ = node;
    } else {
        // The head's previous link already gives us the tail: no scan and
        // no separate tail pointer are needed. Insert between tail and head.
        Node* tail = head_->previous_;
        node->previous_ = tail;
        node->next_ = head_;
        tail->next_ = node;
        head_->previous_ = node;
    }
    ++size_;
    return node;
}

template <typename T>
T* CircularList<T>::popFront() noexcept {
    if (head_ == nullptr) {
        return nullptr;
    }
    T* value = head_->value_;
    remove(head_);
    return value;
}

template <typename T>
T* CircularList<T>::front() const noexcept {
    return head_ == nullptr ? nullptr : head_->value_;
}

template <typename T>
bool CircularList<T>::remove(Node* node) noexcept {
    if (node == nullptr || node->owner_ != this) {
        return false;
    }

    if (size_ == 1) {
        head_ = nullptr;
    } else {
        // Join both neighbours so forward and backward traversal agree.
        node->previous_->next_ = node->next_;
        node->next_->previous_ = node->previous_;
        if (node == head_) {
            head_ = node->next_;
        }
    }
    delete node;
    --size_;
    return true;
}

template <typename T>
void CircularList<T>::rotate() noexcept {
    if (head_ != nullptr) {
        head_ = head_->next_;
    }
}

template <typename T>
bool CircularList<T>::isEmpty() const noexcept { return size_ == 0; }

template <typename T>
std::size_t CircularList<T>::size() const noexcept { return size_; }
