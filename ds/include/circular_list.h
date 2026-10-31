#ifndef B6_SCHEDULER_DS_CIRCULAR_LIST_H
#define B6_SCHEDULER_DS_CIRCULAR_LIST_H

#include <cstddef>

// A circular doubly linked list of non-owning pointers. For example,
// CircularList<Task> stores Task*, without knowing anything about scheduling.
// The list owns its nodes, but the caller owns the pointed-to objects and must
// keep them alive while they are used through the list.
//
// Every listed operation takes O(1) link work; pushBack also allocates one node.
// Destruction takes O(n), and storing n pointers requires O(n) node space.
template <typename T>
class CircularList {
public:
    // A stable handle returned by pushBack. Its links are private so callers
    // can inspect the ring without accidentally breaking it. A handle remains
    // valid until its node is removed or the owning list is destroyed.
    class Node {
    public:
        // Access the stored pointer without transferring ownership. O(1).
        T* getValue() const noexcept;

        // Follow the ring forward, wrapping from the tail to the head. O(1).
        Node* getNext() const noexcept;

        // Follow the ring backward, wrapping from the head to the tail. O(1).
        Node* getPrevious() const noexcept;

    private:
        friend class CircularList<T>;

        // Only the list creates nodes. A new node initially forms its own ring.
        // This also provides the correct links for a one-element list. O(1).
        Node(T* value, const CircularList* owner) noexcept;

        // Only the list destroys nodes; destroying one never deletes value_.
        ~Node() = default;
        Node(const Node&) = delete;
        Node& operator=(const Node&) = delete;

        T* value_;                  // Borrowed object; never deleted here.
        Node* next_;                // The next node in circular order.
        Node* previous_;            // The previous node in circular order.
        const CircularList* owner_; // Allows O(1) rejection of foreign handles.
    };

    // An empty list has no head and allocates no memory. O(1).
    CircularList() noexcept;

    // Release every node, leaving the caller's objects untouched. O(n).
    ~CircularList();

    // Each node belongs to one fixed list. Disabling copying and moving avoids
    // shared node ownership, double deletion, and invalid owner information.
    CircularList(const CircularList&) = delete;
    CircularList& operator=(const CircularList&) = delete;
    CircularList(CircularList&&) = delete;
    CircularList& operator=(CircularList&&) = delete;

    // Append a borrowed pointer and return its node handle. O(1) link work.
    // Null values are rejected, making nullptr an unambiguous empty result for
    // front/popFront. Duplicate non-null pointers are allowed as separate nodes.
    // If allocation fails, new throws and the existing list remains unchanged.
    Node* pushBack(T* value);

    // Remove the head and return its borrowed value, or nullptr when empty.
    // The removed node's handle becomes invalid; the object stays alive. O(1).
    T* popFront() noexcept;

    // Inspect the head's value without changing the order. Empty -> nullptr.
    // Constness protects the list, not the caller-owned object. O(1).
    T* front() const noexcept;

    // Unlink a known node without searching. Null and live foreign handles
    // return false. Otherwise, node must be a live handle from pushBack: a
    // dangling pointer cannot be safely checked and must never be passed here.
    // On success only this node's handle is invalidated. O(1).
    bool remove(Node* node) noexcept;

    // Move the head logically to the tail by advancing the starting position.
    // Links and handles stay unchanged. Empty/single-node lists are unchanged.
    // This is O(1): A,B,C becomes B,C,A without allocating or deleting nodes.
    void rotate() noexcept;

    // Report whether the ring has any nodes. O(1).
    bool isEmpty() const noexcept;

    // Return the maintained count; traversing the ring is unnecessary. O(1).
    std::size_t size() const noexcept;

private:
    Node* head_;       // First node, or nullptr when empty; its previous is tail.
    std::size_t size_; // Number of nodes, updated on every insertion/removal.
};

#include "../../ds/src/circular_list.tpp"

#endif  // B6_SCHEDULER_DS_CIRCULAR_LIST_H
