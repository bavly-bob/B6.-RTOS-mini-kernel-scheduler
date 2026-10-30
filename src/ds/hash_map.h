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

template <typename T>
HashMap<T>::HashMap()
    : buckets_(new Node*[kInitialBucketCount]{}),
      bucketCount_(kInitialBucketCount),
      size_(0) {}

template <typename T>
HashMap<T>::HashMap(const HashMap& other)
    : buckets_(new Node*[other.bucketCount_ == 0 ? kInitialBucketCount : other.bucketCount_]{}),
      bucketCount_(other.bucketCount_ == 0 ? kInitialBucketCount : other.bucketCount_),
      size_(0) {
    for (std::size_t bucket = 0; bucket < other.bucketCount_; ++bucket) {
        for (Node* node = other.buckets_[bucket]; node != nullptr; node = node->next) {
            put(node->key, node->value);
        }
    }
}

template <typename T>
HashMap<T>::HashMap(HashMap&& other) noexcept
    : buckets_(other.buckets_), bucketCount_(other.bucketCount_), size_(other.size_) {
    other.buckets_ = nullptr;
    other.bucketCount_ = 0;
    other.size_ = 0;
}

template <typename T>
HashMap<T>::~HashMap() {
    release();
}

template <typename T>
HashMap<T>& HashMap<T>::operator=(const HashMap& other) {
    if (this != &other) {
        HashMap copy(other);
        *this = static_cast<HashMap&&>(copy);
    }
    return *this;
}

template <typename T>
HashMap<T>& HashMap<T>::operator=(HashMap&& other) noexcept {
    if (this != &other) {
        release();
        buckets_ = other.buckets_;
        bucketCount_ = other.bucketCount_;
        size_ = other.size_;
        other.buckets_ = nullptr;
        other.bucketCount_ = 0;
        other.size_ = 0;
    }
    return *this;
}

template <typename T>
bool HashMap<T>::put(int key, T* value) {
    if (bucketCount_ == 0) {
        resize(kInitialBucketCount);
    }

    Node* existing = findNode(key);
    if (existing != nullptr) {
        existing->value = value;
        return false;
    }

    if ((size_ + 1) * 4 > bucketCount_ * 3) {
        resize(bucketCount_ * 2);
    }

    const std::size_t bucket = bucketIndex(key);
    buckets_[bucket] = new Node(key, value, buckets_[bucket]);
    ++size_;
    return true;
}

template <typename T>
T* HashMap<T>::get(int key) const {
    Node* node = findNode(key);
    return node == nullptr ? nullptr : node->value;
}

template <typename T>
bool HashMap<T>::remove(int key) {
    if (bucketCount_ == 0) {
        return false;
    }

    const std::size_t bucket = bucketIndex(key);
    Node* previous = nullptr;
    Node* current = buckets_[bucket];
    while (current != nullptr) {
        if (current->key == key) {
            if (previous == nullptr) {
                buckets_[bucket] = current->next;
            } else {
                previous->next = current->next;
            }
            delete current;
            --size_;
            return true;
        }
        previous = current;
        current = current->next;
    }
    return false;
}

template <typename T>
bool HashMap<T>::contains(int key) const {
    return findNode(key) != nullptr;
}

template <typename T>
std::size_t HashMap<T>::size() const {
    return size_;
}

template <typename T>
std::size_t HashMap<T>::bucketCount() const {
    return bucketCount_;
}

template <typename T>
void HashMap<T>::resize(std::size_t requestedBucketCount) {
    const std::size_t newBucketCount = normalizedBucketCount(requestedBucketCount);
    if (newBucketCount == bucketCount_ && buckets_ != nullptr) {
        return;
    }

    Node** newBuckets = new Node*[newBucketCount]{};
    for (std::size_t bucket = 0; bucket < bucketCount_; ++bucket) {
        Node* current = buckets_[bucket];
        while (current != nullptr) {
            Node* next = current->next;
            const std::size_t newBucket = hash(current->key) % newBucketCount;
            current->next = newBuckets[newBucket];
            newBuckets[newBucket] = current;
            current = next;
        }
    }

    delete[] buckets_;
    buckets_ = newBuckets;
    bucketCount_ = newBucketCount;
}

template <typename T>
void HashMap<T>::rehash(std::size_t requestedBucketCount) {
    resize(requestedBucketCount);
}

template <typename T>
void HashMap<T>::clear() {
    deleteNodes();
}

template <typename T>
std::size_t HashMap<T>::hash(int key) {
    // Mixes nearby and negative IDs before selecting a bucket.
    std::uint32_t value = static_cast<std::uint32_t>(key);
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return static_cast<std::size_t>(value);
}

template <typename T>
typename HashMap<T>::Node* HashMap<T>::findNode(int key) const {
    if (bucketCount_ == 0) {
        return nullptr;
    }

    for (Node* node = buckets_[bucketIndex(key)]; node != nullptr; node = node->next) {
        if (node->key == key) {
            return node;
        }
    }
    return nullptr;
}

template <typename T>
std::size_t HashMap<T>::bucketIndex(int key) const {
    return hash(key) % bucketCount_;
}

template <typename T>
void HashMap<T>::deleteNodes() {
    for (std::size_t bucket = 0; bucket < bucketCount_; ++bucket) {
        Node* current = buckets_[bucket];
        while (current != nullptr) {
            Node* next = current->next;
            delete current;
            current = next;
        }
        buckets_[bucket] = nullptr;
    }
    size_ = 0;
}

template <typename T>
void HashMap<T>::release() {
    if (buckets_ != nullptr) {
        deleteNodes();
        delete[] buckets_;
    }
    buckets_ = nullptr;
    bucketCount_ = 0;
}

template <typename T>
std::size_t HashMap<T>::normalizedBucketCount(std::size_t requestedBucketCount) {
    std::size_t capacity = kInitialBucketCount;
    while (capacity < requestedBucketCount) {
        capacity *= 2;
    }
    return capacity;
}

#endif  // B6_SCHEDULER_DS_HASH_MAP_H
