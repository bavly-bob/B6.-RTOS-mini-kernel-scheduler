#ifndef B6_SCHEDULER_DS_HASH_MAP_H
#define B6_SCHEDULER_DS_HASH_MAP_H

#include <cstddef>
#include <cstdint>

// Integer-key chained map storing non-owning pointers, such as TCBs. 
template <typename T>
class HashMap {
public:
    HashMap();

    HashMap(const HashMap& other);

    HashMap(HashMap&& other) noexcept;

    ~HashMap();

    HashMap& operator=(const HashMap& other);

    HashMap& operator=(HashMap&& other) noexcept;

    // Inserts or updates a value, O(1) average. 
    bool put(int key, T* value);

    // Returns the value pointer, O(1) average. 
    T* get(int key) const;

    // Removes a key and reports whether it existed. O(1) average. 
    bool remove(int key);

    // Checks key existence. 
    bool contains(int key) const;

    std::size_t size() const;

    std::size_t bucketCount() const;

    // Rebuilds buckets while preserving entries. O(n). 
    void resize(std::size_t requestedBucketCount);

    // Alias for resize. 
    void rehash(std::size_t requestedBucketCount);

    // Removes every entry but retains the buckets. 
    void clear();

    // Custom integer hash function. 
    static std::size_t hash(int key);

private:
    // Collision-chain node; it does not own value.
    struct Node {
        int key;
        T* value;
        Node* next;

        Node(int entryKey, T* entryValue, Node* nextNode)
            : key(entryKey), value(entryValue), next(nextNode) {}
    };

    static constexpr std::size_t kInitialBucketCount = 8;

    // Heads of the collision chains.
    Node** buckets_;
    std::size_t bucketCount_;
    std::size_t size_;

    Node* findNode(int key) const;

    std::size_t bucketIndex(int key) const;

    void deleteNodes();

    void release();

    static std::size_t normalizedBucketCount(std::size_t requestedBucketCount);
};

#include "../../ds/src/hash_map.tpp"

#endif  // B6_SCHEDULER_DS_HASH_MAP_H
