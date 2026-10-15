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

    ~ArcLruPart() {
        auto clearList = [](NodePtr &head) {
            while (head) {
                NodePtr next = head->m_next;
                // head->m_next = nullptr; // 也可以
                head->m_next.reset(); // 断开当前节点到后继的共享指针
                head = next;
            }
        };
        
        clearList(m_mainHead);
        clearList(m_ghostHead);
    }

    bool put(const Key &key, Value value) {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_capacity == 0) return false;

        auto it = m_mainCache.find(key);
        if (it != m_mainCache.end()) {
            return updateExistingNode(it->second, value);
        }
        return addNewNode(key, value);
    }

    bool get(const Key &key, Value &value, bool &shouldTransform) {
        std::lock_guard<std::mutex> lock(m_mutex);
        shouldTransform = false;
        auto it = m_mainCache.find(key);
        if (it != m_mainCache.end()) {
            shouldTransform = updateNodeFreq(it->second);
            value = it->second->getValue();
            return true;
        }
        return false;
    }

    Value get(const Key &key, bool &shouldTransform) {
        Value value{};
        get(key, value, shouldTransform);
        return value;
    }

    bool checkGhost(const Key &key) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_ghostCache.find(key);
        if (it != m_ghostCache.end()) {
            removeFromGhost(it->second);
            m_ghostCache.erase(it);
            return true;
        }
        return false;
    }

    void increaseCapacity() {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_capacity;
    }

    bool decreaseCapacity() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_capacity <= 1) return false;
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
    }

    bool updateExistingNode(NodePtr node, const Value &value) {
        node->setValue(value);
        moveToTail(node);
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
        m_mainCache.emplace(key, newNode);
        addToTail(newNode);
        return true;
    }

    void evictLeastRecent() {
        NodePtr leastRecentNode = m_mainHead->m_next;
        if (!leastRecentNode || leastRecentNode == m_mainTail)
            return;
        
        // 从主链表删除
        removeFromMain(leastRecentNode);

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
        if (m_ghostCapacity == 0) return;

        // 删除同一个key对应的旧幽灵节点
        auto it = m_ghostCache.find(node->getKey());
        if (it != m_ghostCache.end()) {
            // NodePtr oldNode = it->second;
            removeFromGhost(it->second);
            m_ghostCache.erase(it);
        }
        
        // 重置节点访问频次
        node->m_freq = 1;

        // 添加到幽灵缓存映射
        m_ghostCache.emplace(node->getKey(), node); 

        // 添加到幽灵缓存尾部
        NodePtr prev = m_ghostTail->m_prev.lock();
        prev->m_next = node;
        node->m_prev = prev;
        
        node->m_next = m_ghostTail;
        m_ghostTail->m_prev = node;
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