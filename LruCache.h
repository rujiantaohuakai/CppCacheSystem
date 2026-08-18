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

}