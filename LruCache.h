#pragma once

#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "ICachePolicy.h"

namespace CppCache
{

// 向前声明
template<typename Key, typename Value> class LruCache;

template<typename Key, typename Value>
class LruNode
{
private:
    Key _key;
    Value _value;
    size_t _accessCount; // 访问次数
    std::weak_ptr<LruNode<Key, Value>> _prev; // 防止循环引用
    std::shared_ptr<LruNode<Key, Value>> _next;

public:
    LruNode(Key key, Value value)
        : _key(key)
        , _value(value)
        , _accessCount(1)
    {}

    // 访问器
    Key getKey() const { return _key; }
    Value getValue() const { return _value; }
    void setValue(const Value& value) { _value = value; }
    size_t getAccessCount() const { return _accessCount; }
    void incrementAccessCount() { ++_accessCount; }

    friend class LruCache<Key, Value>;

};

template<typename Key, typename Value>
class LruCache : public ICachePolicy<Key, Value>
{
public:
    using LruNodeType = LruNode<Key, Value>;
    using NodePtr = std::shared_ptr<LruNodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;

    LruCache(int capacity)
        : _capacity{capacity}
    {
        initializeList();
    }

    ~LruCache() override = default;

    // 添加缓存
    void put(Key key, Value value) override
    {
        if (_capacity <= 0)
            return;

        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _nodeMap.find(key);
        if (it != _nodeMap.end()) {
            // 如果在当前容器中，则更新value，并调用get方法，代表该数据刚被访问
            updateExistingNode(it->second, value);
            return;
        }

        addNewNode(key, value);
    }

    bool get(Key key, Value &value) override
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _nodeMap.find(key);
        if (it != _nodeMap.end()) {
            moveToMostRecent(it->second);
            value = it->second->getValue();
            return true;
        }
        return false;
    }

    Value get(Key key) override
    {
        Value value{};
        get(key, value);
        return value;
    }

    void remove(Key key)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _nodeMap.find(key);
        if (it != _nodeMap.end()) {
            removeNode(it->second);
            _nodeMap.erase(it);
        }
    }

private:
    void initializeList()
    {
        // 创建首尾虚拟节点
        _dummyHead = std::make_shared<LruNodeType>(Key(), Value());
        _dummyTail = std::make_shared<LruNodeType>(Key(), Value());
        _dummyHead->_next = _dummyTail;
        _dummyTail->_prev = _dummyHead;
    }

    void updateExistingNode(NodePtr node, const Value& value)
    {
        node->setValue(value);
        moveToMostRecent(node);
    }


    void addNewNode(const Key& key, const Value& value)
    {
        if (_nodeMap.size() >= _capacity)
        {
            evictLeastRecent();
        }

        NodePtr newNode = std::make_shared<LruNodeType>(key, value);
        insertNode(newNode);
        _nodeMap[key] = newNode;
    }

    // 将该结点移动到最新位置
    void moveToMostRecent(NodePtr node)
    {
        removeNode(node);
        insertNode(node);
    }

    // 移除结点
    void removeNode(NodePtr node)
    {
        if (!node->_prev.expired() && node->_next)
        {
            NodePtr prev = node->_prev.lock();
            prev->_next = node->_next;
            node->_next->_prev = prev;
            node->_next = nullptr; // 清空_next指针，断开结点
        }
    }

    // 从尾部插入结点
    void insertNode(NodePtr node)
    {
        node->_next = _dummyTail;
        node->_prev = _dummyTail->_prev;
        _dummyTail->_prev.lock()->_next = node;
        _dummyTail->_prev = node;
    }

    // 驱逐最近最少访问
    void evictLeastRecent()
    {
        NodePtr leastRecent = _dummyHead->_next;
        removeNode(leastRecent);
        _nodeMap.erase(leastRecent->getKey());
    }



private:
    int         _capacity; // 缓存容量
    NodeMap     _nodeMap; // key -> node
    std::mutex  _mutex;
    NodePtr     _dummyHead; // 虚拟头结点
    NodePtr     _dummyTail; // 虚拟尾结点
};

template<typename Key, typename Value>
class LruKCache : public LruCache<Key, Value>
{
public:
    LruKCache(int capacity, int historyCapacity, int k)
        : LruCache<Key, Value>(capacity)
        , _historyList(std::make_unique<LruCache<Key, size_t>>(historyCapacity))
        , _k(k)
    {}

    Value get(Key key) override
    {
        // 首先尝试从主缓存获取数据
        Value value{};
        
        get(key, value);
        
        return value;
    }

    bool get(Key key, Value &value) override
    {
        std::lock_guard<std::mutex> lock(_mutex_k);

        bool inMainCache = LruCache<Key, Value>::get(key, value);

        // 如果数据在主缓存中，直接返回
        if (inMainCache)
        {
            return true;
        }

        // 获取并更新访问历史计数
        // 如果未在_historyList, historyCount为0，因为get(key)中Value value{};
        // size_t value{}; value默认值为0
        size_t historyCount = _historyList->get(key);
        ++historyCount;
        _historyList->put(key, historyCount);

        // 如果数据不在主缓存，但访问次数达到了k次
        if (historyCount >= _k)
        {
            // 检查是否有历史值记录
            auto it = _historyValueMap.find(key);
            if (it != _historyValueMap.end())
            {
                // 有历史值且>=k，记录并将其添加到主缓存
                value = it->second;

                // 从历史记录移除
                _historyList->remove(key);
                _historyValueMap.erase(it);

                // 添加到主缓存
                LruCache<Key, Value>::put(key, value);

                return true;
            }
            // 没有历史值记录，无法添加到缓存，返回默认值
        }
        // 数据不在主缓存且不满足添加条件，返回false
        return false;
    }

    void put(Key key, Value value) override
    {
        std::lock_guard<std::mutex> lock(_mutex_k);

        // 检查是否已在主缓存
        Value existingValue{};
        bool inMainCache = LruCache<Key, Value>::get(key, existingValue);

        if (inMainCache)
        {
            // 已在主缓存，直接更新
            LruCache<Key, Value>::put(key, value);
            return;
        }

        // 获取并更新访问历史
        size_t historyCount = _historyList->get(key);
        ++historyCount;
        _historyList->put(key, historyCount);

        // 保存值到历史记录映射，供后续get操作使用
        _historyValueMap[key] = value;

        // 检查是否到达k次访问阈值
        if (historyCount >= _k)
        {
            // 添加到主缓存
            _historyList->remove(key);
            _historyValueMap.erase(key);
            LruCache<Key, Value>::put(key, value);
        }
    }

private:
    int _k; // 进入缓存队列的评判标准
    std::unique_ptr<LruCache<Key, size_t>> _historyList; // 访问数据历史记录（value为访问次数）
    std::unordered_map<Key, Value> _historyValueMap; // 存储未达到k次访问的数据值 
    std::mutex _mutex_k;
};














} // namesapce