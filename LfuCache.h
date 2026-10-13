#pragma once

#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <stdexcept>
#include <chrono>
#include <algorithm>
#include <cstdint>

#include "ICachePolicy.h"

namespace CppCache {

template<typename Key, typename Value> class LfuCache;

template<typename Key, typename Value> class FreqList;

template<typename Key, typename Value>
class LfuNode {
private:
    Key m_key;
    Value m_value;
    size_t m_freq; // 最近访问次数
    std::weak_ptr<LfuNode<Key, Value>> m_prev;
    std::shared_ptr<LfuNode<Key, Value>> m_next;

    friend class FreqList<Key, Value>;
    friend class LfuCache<Key, Value>;
public:
    LfuNode() : m_freq(1), m_next(nullptr)
    {}

    LfuNode(Key key, Value value, size_t freq)
    // std::weak_ptr不能用nullptr构造
    : m_key(key), m_value(value), m_freq(freq), m_prev(), m_next(nullptr)
    {}

    // 访问器
    void setValue(const Value &value) {
        m_value = value;
    }
    Value getValue() const {
        return m_value;
    }
    Key getKey() const {
        return m_key;
    }
};

template<typename Key, typename Value>
class FreqList {
    using LfuNodeType = LfuNode<Key, Value>;
    using NodePtr = std::shared_ptr<LfuNodeType>;
    
private:
    size_t m_freq;
    NodePtr m_dummyHead;
    NodePtr m_dummyTail;
    friend class LfuCache<Key, Value>;

public:
    FreqList(size_t freq)
    : m_freq(freq)
    {
        m_dummyHead = std::make_shared<LfuNodeType>(Key(), Value(), 0);
        m_dummyTail = std::make_shared<LfuNodeType>(Key(), Value(), 0);
        m_dummyHead->m_next = m_dummyTail;
        m_dummyTail->m_prev = m_dummyHead;
    }

    bool isEmpty() const {
        return m_dummyHead->m_next == m_dummyTail;
    }

};

template<typename Key, typename Value>
class LfuCache : public ICachePolicy<Key, Value> {
    using LfuNodeType = LfuNode<Key, Value>;
    using NodePtr = std::shared_ptr<LfuNode<Key, Value>>;
    using FreqListType = FreqList<Key, Value>;
    using FreqListPtr = std::shared_ptr<FreqListType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;
    using FreqMap = std::unordered_map<size_t, FreqListPtr>;

private:
    size_t m_capacity;
    size_t m_minFreq;
    std::mutex m_mutex;
    NodeMap m_nodeMap;
    FreqMap m_freqMap;
    size_t m_maxAverageNum;
    size_t m_currentAverageNum;
    size_t m_currentTotalNum;
public:
    explicit LfuCache(size_t capacity)
    : m_capacity(capacity), m_minFreq(1), m_maxAverageNum(100000000), m_currentAverageNum(0), m_currentTotalNum(0)
    {}
    explicit LfuCache(size_t capacity, size_t maxAverageNum)
    : m_capacity(capacity), m_minFreq(1), m_maxAverageNum(maxAverageNum), m_currentAverageNum(0), m_currentTotalNum(0)
    {
        if (m_maxAverageNum == 0) {
            throw std::invalid_argument("maxAverageNum must be at least 1");
        }
    }

    ~LfuCache() override = default;
    // 缓存接口
    void put(const Key &key, Value value) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (m_capacity == 0) return; //缓存空间为 0 ，不可以加入任何数据

        auto it = m_nodeMap.find(key);
        if (it != m_nodeMap.end()) {
            // 如果已经在缓存中
             it->second->m_value = value; // 更新值
            increaseFrequency(it->second);
            return;
        }
        
        NodePtr newNode = std::make_shared<LfuNodeType>(key, value, 1);

        if (m_nodeMap.size() == m_capacity) {
            // 缓存已满，删除频次最少中最久未使用的节点
            FreqListPtr minList = m_freqMap.at(m_minFreq);
            NodePtr delNode = minList->m_dummyHead->m_next;

            // 从链表和哈希表中删除
            deleteNodeFromFreqList(delNode);
            m_nodeMap.erase(delNode->m_key);
            decreaseCurrentAverageNum(delNode->m_freq);

            if (minList->isEmpty()) {
                m_freqMap.erase(m_minFreq);
            }
        }
        // emplace(key, newNode) 在键不存在时直接构造并插入元素，键已存在时保留原值；
        // 返回 pair<iterator, bool>，分别表示对应元素的位置和是否插入成功。
        // m_nodeMap[key] = newNode 在键不存在时先插入默认值再赋值，键已存在时覆盖原值，
        // 不返回是否插入成功的标志。
        m_nodeMap.emplace(key, newNode);
        addNodeToFreqList(newNode);
        increaseCurrentAverageNum();

        m_minFreq = 1;
    }
    // 传入key，访问到的值以传出参数形式返回。访问成功返回true
    bool get(const Key &key, Value &value) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_nodeMap.find(key);
        if (it == m_nodeMap.end()) return false;
        NodePtr node = it->second;
        
        // 获取值
        value = node->m_value;

        increaseFrequency(node);
        
        
        return true;
        
    }
    // 缓存中有key，返回value
    Value get(const Key &key) override {
        Value value{};
        get(key, value);
        return value;
    }

private:
    void deleteNodeFromFreqList(NodePtr node) {
        NodePtr prev = node->m_prev.lock();
        prev->m_next = node->m_next;
        node->m_next->m_prev = node->m_prev;
    }

    void addNodeToFreqList(NodePtr node) {
        auto it = m_freqMap.find(node->m_freq);
        if (it == m_freqMap.end()) {
            it = m_freqMap.emplace(
                node->m_freq,
                std::make_shared<FreqListType>(node->m_freq)
            ).first;
        }

        FreqListPtr list = it->second;
        // 最新节点加入队尾
        NodePtr tail = list->m_dummyTail;
        NodePtr prev = tail->m_prev.lock();

        prev->m_next = node;
        node->m_prev = prev;

        node->m_next = tail;
        tail->m_prev = node;
    }

    void increaseFrequency(NodePtr &node) {

        // 获取node现在所在的FreqList
        const size_t oldFreq = node->m_freq;
        FreqListPtr oldList = m_freqMap.at(oldFreq);

        // 删除原FreqList中Node
        deleteNodeFromFreqList(node);

        if (oldList->isEmpty()) {
            // 如果list为空则
            m_freqMap.erase(oldFreq);
            if (oldFreq == m_minFreq) {
                m_minFreq++;
            }
        }

        // freq+1
        node->m_freq++;

        // 加入到新FreqList中
        addNodeToFreqList(node);
        increaseCurrentAverageNum();
    }

    void increaseCurrentAverageNum() {
        m_currentTotalNum++;

        if (m_nodeMap.empty()) {
            m_currentAverageNum = 0;
        }
        else {
            m_currentAverageNum = m_currentTotalNum / m_nodeMap.size();
        }   

        if (m_currentAverageNum > m_maxAverageNum) {
            handleOverMaxAverageNum();
        }
    }

    void decreaseCurrentAverageNum(size_t num) {
        m_currentTotalNum -= num;
        if (m_nodeMap.empty()) {
            m_currentAverageNum = 0;
        }
        else {
            m_currentAverageNum = m_currentTotalNum / m_nodeMap.size();
        }

    }

    void handleOverMaxAverageNum() {
        // 所有节点减少freq
        if (m_nodeMap.empty()) return;

        const size_t decrease = std::max<size_t>(1, m_maxAverageNum / 2);

        size_t newTotal = 0;

        for (auto it = m_nodeMap.begin(); it != m_nodeMap.end(); ++it) {
            if (!it->second) return;
            NodePtr node = it->second;

            deleteNodeFromFreqList(node);

            node->m_freq = node->m_freq > decrease
                        ? node->m_freq - decrease
                        : size_t(1);

            addNodeToFreqList(node);
            newTotal += node->m_freq;
        }
        m_currentTotalNum = newTotal;
        m_currentAverageNum = m_currentTotalNum / m_nodeMap.size();
        updateMinFreq();
    }

    void updateMinFreq() {
        m_minFreq = SIZE_MAX;
        for (const auto &pair : m_freqMap) {
            if (pair.second && !pair.second->isEmpty()) {
                m_minFreq = std::min(m_minFreq, pair.first);
            }
        }
        if (m_minFreq == SIZE_MAX) {
            m_minFreq = 1;
        }
    }
};









}
