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
    Key _key;
    Value _value;
    size_t _freq; // 最近访问次数
    std::weak_ptr<LfuNode<Key, Value>> _prev;
    std::shared_ptr<LfuNode<Key, Value>> _next;

    friend class FreqList<Key, Value>;
    friend class LfuCache<Key, Value>;
public:
    LfuNode() : _freq(1), _next(nullptr)
    {}

    LfuNode(Key key, Value value, size_t freq)
    // std::weak_ptr不能用nullptr构造
    : _key(key), _value(value), _freq(freq), _prev(), _next(nullptr)
    {}

    // 访问器
    void setValue(const Value &value) {
        _value = value;
    }
    Value getValue() const {
        return _value;
    }
    Key getKey() const {
        return _key;
    }
};

template<typename Key, typename Value>
class FreqList {
    using LfuNodeType = LfuNode<Key, Value>;
    using NodePtr = std::shared_ptr<LfuNodeType>;
    
private:
    size_t _freq;
    NodePtr _dummyHead;
    NodePtr _dummyTail;
    friend class LfuCache<Key, Value>;

public:
    FreqList(size_t freq)
    : _freq(freq)
    {
        _dummyHead = std::make_shared<LfuNodeType>(Key(), Value(), 0);
        _dummyTail = std::make_shared<LfuNodeType>(Key(), Value(), 0);
        _dummyHead->_next = _dummyTail;
        _dummyTail->_prev = _dummyHead;
    }

    bool isEmpty() const {
        return _dummyHead->_next == _dummyTail;
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
    size_t _capacity;
    size_t _minFreq;
    std::mutex _mutex;
    NodeMap _nodeMap;
    FreqMap _freqMap;
    size_t _maxAverageNum;
    size_t _currentAverageNum;
    size_t _currentTotalNum;
public:
    explicit LfuCache(size_t capacity)
    : _capacity(capacity), _minFreq(1), _maxAverageNum(100000000), _currentAverageNum(0), _currentTotalNum(0)
    {}
    explicit LfuCache(size_t capacity, size_t maxAverageNum)
    : _capacity(capacity), _minFreq(1), _maxAverageNum(maxAverageNum), _currentAverageNum(0), _currentTotalNum(0)
    {
        if (_maxAverageNum == 0) {
            throw std::invalid_argument("maxAverageNum must be at least 1");
        }
    }

    ~LfuCache() override = default;
    // 缓存接口
    void put(const Key &key, Value value) override {
        std::lock_guard<std::mutex> lock(_mutex);
        
        if (_capacity == 0) return; //缓存空间为 0 ，不可以加入任何数据

        auto it = _nodeMap.find(key);
        if (it != _nodeMap.end()) {
            // 如果已经在缓存中
             it->second->_value = value; // 更新值
            increaseFrequency(it->second);
            return;
        }
        
        NodePtr newNode = std::make_shared<LfuNodeType>(key, value, 1);

        if (_nodeMap.size() == _capacity) {
            // 缓存已满，删除频次最少中最久未使用的节点
            FreqListPtr minList = _freqMap.at(_minFreq);
            NodePtr delNode = minList->_dummyHead->_next;

            // 从链表和哈希表中删除
            deleteNodeFromFreqList(delNode);
            _nodeMap.erase(delNode->_key);
            decreaseCurrentAverageNum(delNode->_freq);

            if (minList->isEmpty()) {
                _freqMap.erase(_minFreq);
            }
        }
        // emplace(key, newNode) 在键不存在时直接构造并插入元素，键已存在时保留原值；
        // 返回 pair<iterator, bool>，分别表示对应元素的位置和是否插入成功。
        // _nodeMap[key] = newNode 在键不存在时先插入默认值再赋值，键已存在时覆盖原值，
        // 不返回是否插入成功的标志。
        _nodeMap.emplace(key, newNode);
        addNodeToFreqList(newNode);
        increaseCurrentAverageNum();

        _minFreq = 1;
    }
    // 传入key，访问到的值以传出参数形式返回。访问成功返回true
    bool get(const Key &key, Value &value) override {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _nodeMap.find(key);
        if (it == _nodeMap.end()) return false;
        NodePtr node = it->second;
        
        // 获取值
        value = node->_value;

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
        NodePtr prev = node->_prev.lock();
        prev->_next = node->_next;
        node->_next->_prev = node->_prev;
    }

    void addNodeToFreqList(NodePtr node) {
        auto it = _freqMap.find(node->_freq);
        if (it == _freqMap.end()) {
            it = _freqMap.emplace(
                node->_freq,
                std::make_shared<FreqListType>(node->_freq)
            ).first;
        }

        FreqListPtr list = it->second;
        // 最新节点加入队尾
        NodePtr tail = list->_dummyTail;
        NodePtr prev = tail->_prev.lock();

        prev->_next = node;
        node->_prev = prev;

        node->_next = tail;
        tail->_prev = node;
    }

    void increaseFrequency(NodePtr &node) {

        // 获取node现在所在的FreqList
        const size_t oldFreq = node->_freq;
        FreqListPtr oldList = _freqMap.at(oldFreq);

        // 删除原FreqList中Node
        deleteNodeFromFreqList(node);

        if (oldList->isEmpty()) {
            // 如果list为空则
            _freqMap.erase(oldFreq);
            if (oldFreq == _minFreq) {
                _minFreq++;
            }
        }

        // freq+1
        node->_freq++;

        // 加入到新FreqList中
        addNodeToFreqList(node);
        increaseCurrentAverageNum();
    }

    void increaseCurrentAverageNum() {
        _currentTotalNum++;

        if (_nodeMap.empty()) {
            _currentAverageNum = 0;
        }
        else {
            _currentAverageNum = _currentTotalNum / _nodeMap.size();
        }   

        if (_currentAverageNum > _maxAverageNum) {
            handleOverMaxAverageNum();
        }
    }

    void decreaseCurrentAverageNum(size_t num) {
        _currentTotalNum -= num;
        if (_nodeMap.empty()) {
            _currentAverageNum = 0;
        }
        else {
            _currentAverageNum = _currentTotalNum / _nodeMap.size();
        }

    }

    void handleOverMaxAverageNum() {
        // 所有节点减少freq
        if (_nodeMap.empty()) return;

        const size_t decrease = std::max<size_t>(1, _maxAverageNum / 2);

        size_t newTotal = 0;

        for (auto it = _nodeMap.begin(); it != _nodeMap.end(); ++it) {
            if (!it->second) return;
            NodePtr node = it->second;

            deleteNodeFromFreqList(node);

            node->_freq = node->_freq > decrease
                        ? node->_freq -decrease
                        : size_t(1);

            addNodeToFreqList(node);
            newTotal += node->_freq;
        }
        _currentTotalNum = newTotal;
        _currentAverageNum = _currentTotalNum / _nodeMap.size();
        updateMinFreq();
    }

    void updateMinFreq() {
        _minFreq = SIZE_MAX;
        for (const auto &pair : _freqMap) {
            if (pair.second && !pair.second->isEmpty()) {
                _minFreq = std::min(_minFreq, pair.first);
            }
        }
        if (_minFreq == SIZE_MAX) {
            _minFreq = 1;
        }
    }
};









}
