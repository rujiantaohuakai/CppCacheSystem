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

    bool put(const Key &key, Value value) {
        if (m_capacity == 0) return false;

        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_mainCache.find(key);
        if (it != m_mainCache.end()) {
            return updateExistingNode(it->second, value);
        }
        return addNewNode(key, value);
    }

    bool get(const Key &key, Value &value, bool &shouldTransform) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_mainCache.find(key);
        if (it != m_mainCache.end()) {
            shouldTransform = updateNodeFreq(it->second);
            value = it->second->getValue();
            return true;
        }
        return false;
    }

    Value get(const Key &key, bool &shouldTransform) {
        Value value();
        get(key, value, shouldTransform);
        return value;
    }

    bool CheckGhost(Key key) {
        auto it = m_ghostCache.find(key);
        if (it != m_ghostCache.end()) {
            removeFromGhost(it->second);
            m_ghostCache.erase(it);
            return true;
        }
        return false;
    }

    void increaseCapacity() {
        ++m_capacity;
    }

    bool decreaseCapacity() {
        if (m_capacity <= 0) return false;
        if (m_mainCache.size() == m_capacity) {
            evictLeastRecent();
        }
        --m_capacity;
        return true;
    }

private:

    void initializeLists() {
        m_mainHead = std::make_shared<NodeType>(Key(), Value());
        m_mainTail = std::make_shared<NodeType>(Key(), Value());
        m_mainHead->m_next = m_mainTail;
        m_mainTail->m_prev = m_mainHead;
        

        m_ghostHead = std::make_shared<NodeType>(Key(), Value());
        m_ghostTail = std::make_shared<NodeType>(Key(), Value());
        m_ghostHead->m_next = m_ghostTail;
        m_ghostTail->m_prev = m_ghostHead;

        NodeType *raw = m_mainHead.get();
        raw->m_next = m_mainTail;

    }

    bool updateExistingNode(NodePtr node, const Value &value) {
        node->setValue(value);
        moveToFront(node);
        return true;
    }

    void moveToTail(NodePtr node) {
        // 从当前位置删除
        removeFromMain(node);

        addToTail(node);
    }

    void addToTail(NodePtr node) {
        NodePtr prev = m_mainTail->m_prev.lock();
        prev->m_next = node;
        node->m_prev = prev;
        node->m_next = m_mainTail;
        m_mainTail->m_prev = node;
    }

    bool addNewNode(const Key &key, const Value &value) {
        if (m_mainCache.size() >= m_capacity) {
            evictLeastRecent();
        }
        NodePtr newNode = std::make_shared<NodeType>(key, value);
        m_mainCache.insert(key, newNode);
        addToTail(newNode);
        return true;
    }

    void evictLeastRecent() {
        NodePtr leastRecentNode = m_mianHead->m_next;
        if (!leastRecentNode || leastRecentNode == m_mainTail)
            return;
        
        // 从主链表删除
        removeFromMain(leastRecent);

        // 加入GhostCache
        if (m_ghostCache.size() >= m_ghostCapacity) {
            removeOldestGhost();
        }
        addToGhost(leastRecentNode);
        m_mainCache.erase(leastRecentNode->getKey());
    }

    void removeFromMain(NodePtr node) {
        if (!node->m_prev.expired() && node->m_next) {
            NodePtr prev = node->m_prev.lock();
            prev->m_next = node->m_next;
            node->m_next->m_prev = node->m_prev;
            node->m_next = nullptr;
        }
    }

    void removeFromGhost(NodePtr node) {
        if (!node->m_prev.expired() && node->m_next) {
            NodePtr prev = node->m_prev.lock();
            prev->m_next = node->m_next;
            node->m_next->m_prev = node->m_prev;
            node->m_next = nullptr;
        }
    }

    void addToGhost(NodePtr node) {
        // 重置节点访问频次
        node->m_freq = 1;

        // 添加到幽灵缓存尾部
        NodePtr prev = m_ghostTail->m_prev.lock();
        prev->m_next = node;
        node->m_prev = prev;
        
        node->m_next = m_ghostTail;
        m_ghostTail->m_prev = node;

        // 添加到幽灵缓存映射
        m_ghostCache.insert(node->getKey(), node);
    }

    void removeOldestGhost() {
        NodePtr oldestGhostNode = m_ghostHead->m_next;
        if (!oldestGhostNode || oldestGhostNode == m_ghostTail)
            return;
        removeFromGhost(oldestGhostNode);
        m_ghostCache.erase(oldestGhostNode->getKey());
    }

    bool updateNodeFreq(NodePtr node) {
        moveToTail(node);
        node->incrementFrequency();
        return node->getFreq() >= m_transformThreshold;
    }

}; 




} // namespace CppCache