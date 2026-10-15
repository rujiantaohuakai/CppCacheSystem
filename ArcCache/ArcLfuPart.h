#pragma once

#include "ArcNode.h"
#include <unordered_map>
#include <mutex>
#include <list>
#include <algorithm> // std::min
#include <cstdint>   // SIZE_MAX

namespace CppCache {

template<typename Key, typename Value>
class ArcLfuPart {
    using NodeType = ArcNode<Key, Value>;
    using NodePtr = std::shared_ptr<NodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;
    // using FreqList = std::list<NodePtr>;
    using FreqMap = std::unordered_map<size_t, std::list<NodePtr>>;

private:
    size_t m_capacity;
    size_t m_ghostCapacity;
    size_t m_transformThreshold; // Lru转换为Lfu门槛值
    size_t m_minFreq;
    std::mutex m_mutex;

    NodeMap m_mainCache;
    NodeMap m_ghostCache;
    FreqMap m_freqMap;

    // 淘汰链表
    NodePtr m_ghostHead;
    NodePtr m_ghostTail;

public:
    explicit ArcLfuPart(size_t capacity, size_t transformThreshold)
    : m_capacity(capacity)
    , m_ghostCapacity(capacity)
    , m_transformThreshold(transformThreshold)
    , m_minFreq(0)
    {
        initializeLists();
    }

    ~ArcLfuPart() {
        while (m_ghostHead) {
            NodePtr next = m_ghostHead->m_next;
            // m_ghostHead->m_next = nullptr; // 也可以
            m_ghostHead->m_next.reset(); // 断开当前节点到后继的共享指针
            m_ghostHead = next;
        }
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

    bool get(const Key &key, Value &value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_mainCache.find(key);
        if (it != m_mainCache.end()) {
            updateNodeFrequency(it->second);
            value = it->second->getValue();
            return true;
        }
        return false;
    }

    Value get(const Key &key) {
        Value value{};
        get(key, value);
        return value;
    }

    bool contain(const Key &key) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_mainCache.find(key) != m_mainCache.end();
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
            evictLeastFrequent();
        }
        --m_capacity;
        return true;
    }

private:
    void initializeLists() {
        m_ghostHead = std::make_shared<NodeType>(Key(), Value());
        m_ghostTail = std::make_shared<NodeType>(Key(), Value());
        m_ghostHead->m_next = m_ghostTail;
        m_ghostTail->m_prev = m_ghostHead;
    }

    bool addNewNode(const Key &key, const Value &value) {
        if (m_mainCache.size() >= m_capacity) {
            evictLeastFrequent();
        }

        NodePtr newNode = std::make_shared<NodeType>(key, value);
        m_mainCache.emplace(key, newNode);

        // 将新节点添加到1频率列表中
        m_freqMap[1].push_back(newNode);
        m_minFreq = 1;
        return true;
    }

    void updateNodeFrequency(NodePtr node) {
        size_t oldFreq = node->getFreq();
        node->incrementFrequency();
        size_t newFreq = node->getFreq();

        // 从旧频率列表中移除
        auto &oldList = m_freqMap[oldFreq];
        oldList.remove(node);

        // 如果链表空了，移除并更新m_minFreq
        if (oldList.empty()) {
            m_freqMap.erase(oldFreq);
            if (oldFreq == m_minFreq) {
                m_minFreq = newFreq;
            }
        }

        // 添加到新的频率列表
        m_freqMap[newFreq].push_back(node);
    }

    bool updateExistingNode(NodePtr node, const Value &value) {
        node->setValue(value);
        updateNodeFrequency(node);
        return true;
    }

    void evictLeastFrequent() {
        if (m_freqMap.empty()) return;
        // 获取最小频率的列表
        auto &minFreqList = m_freqMap[m_minFreq];
        if (minFreqList.empty()) return;

        // 移除使用最少的节点
        NodePtr leastNode = minFreqList.front();
        minFreqList.pop_front();

        // 如果频率列表为空删除该频率列表
        if (minFreqList.empty()) {
            m_freqMap.erase(m_minFreq);
            // 更新最小频率
            updateMinFreq();
        }

        // 将节点移动到幽灵缓存
        if (m_ghostCache.size() >= m_ghostCapacity) {
            removeOldestGhost();
        }
        addToGhost(leastNode);

        // 从主缓存中移除
        m_mainCache.erase(leastNode->getKey());
    }

    void removeOldestGhost() {
        NodePtr oldestGhostNode = m_ghostHead->m_next;
        if (oldestGhostNode != m_ghostTail) {
            removeFromGhost(oldestGhostNode);
            m_ghostCache.erase(oldestGhostNode->getKey());
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

        // 添加到幽灵缓存映射
        m_ghostCache.emplace(node->getKey(), node); 

        // 添加到幽灵缓存尾部
        NodePtr prev = m_ghostTail->m_prev.lock();
        prev->m_next = node;
        node->m_prev = prev;
        
        node->m_next = m_ghostTail;
        m_ghostTail->m_prev = node;
    }

    void updateMinFreq() {
        m_minFreq = SIZE_MAX;
        for (const auto &pair : m_freqMap) {
            // pair.second是std::list<NodePtr>
            if (!pair.second.empty()) {
                m_minFreq = std::min(m_minFreq, pair.first);
            }
        }
        if (m_minFreq == SIZE_MAX) {
            m_minFreq = 1;
        }
    }

    void removeFromGhost(NodePtr node) {
        if (!node->m_prev.expired() && node->m_next) {
            auto prev = node->m_prev.lock();
            prev->m_next = node->m_next;
            node->m_next->m_prev = prev;
            node->m_next = nullptr; // 清空指针
        }
    }



    



};







}

