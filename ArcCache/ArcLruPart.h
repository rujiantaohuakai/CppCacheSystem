#pragma once

#include "ArcNode.h"
#include <unordered_map>
#include <mutex>

namespace CppCache {

template<typename Key, typename Value>
class ArcLruPart {
    using NodeType = ArcNode<Key, Value>;
    using NodePtr = std::shared_ptr<NodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;

private:

    size_t m_capacity;
    size_t m_ghostCapacity;
    size_t m_transformThreshold; // Lru转换为Lfu门槛值
    std::mutex m_mutex;

    NodeMap m_mainCache;
    NodeMap m_ghostCache;

    // 主链表
    NodePtr m_mainHead;
    NodePtr m_mainTail;

    // 淘汰链表
    NodePtr m_ghostHead;
    NodePtr m_ghostTail;

public:

    explicit ArcLruPart(size_t capacity, size_t transformThreshold)
    : m_capacity(capacity)
    , m_ghostCapacity(capacity)
    , m_transformThreshold(transformThreshold)
    {
        initializeLists();
    }

    bool put(Key, key, Value value) {

    }

    bool get(Key key, Value &value, bool &shouldTransform) {

    }

    bool CheckGhost(Key, key) {

    }

    void increaseCapacity() {

    }

    bool decreaseCapacity() {

    }

private:

    void initializeLists() {

    }

    bool updateExistingNode(NodePtr node, const Value &value) {

    }

    bool addNewNode(const Key &key, const Value &value) {

    }

    bool updateNodeAccess(NodePtr node) {

    }

    void moveToFront(NodePtr node) {

    }

    void evictLeastRecent() {

    }

    void removeFromMain(NotePtr node) {

    }

    void removeFromGhost(NodePtr node) {

    }

    void addToGhost(NodePtr node) {

    }

    void removeOldestGhost() {

    }

} // namespace CppCache












}