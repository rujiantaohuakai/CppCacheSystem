#pragma once

#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <stdexcept>
#include <chrono>

#include "ICachePolicy.h"

namespace CppCache
{

// 向前声明
template<typename Key, typename Value> class LruCache;

template<typename Key, typename Value>
class LruNode
{
private:
    Key m_key;
    Value m_value;
    size_t m_accessCount; // 访问次数
    std::weak_ptr<LruNode<Key, Value>> m_prev; // 防止循环引用
    std::shared_ptr<LruNode<Key, Value>> m_next;

public:
    LruNode(Key key, Value value)
        : m_key(key)
        , m_value(value)
        , m_accessCount(1)
    {}

    // 访问器
    Key getKey() const { return m_key; }
    Value getValue() const { return m_value; }
    void setValue(const Value& value) { m_value = value; }
    size_t getAccessCount() const { return m_accessCount; }
    void incrementAccessCount() { ++m_accessCount; }

    friend class LruCache<Key, Value>;

};

template<typename Key, typename Value>
class LruCache : public ICachePolicy<Key, Value>
{
public:
    using LruNodeType = LruNode<Key, Value>;
    using NodePtr = std::shared_ptr<LruNodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;

    explicit LruCache(size_t capacity)
        : m_capacity{validateCapacity(capacity)}
    {
        initializeList();
    }

    ~LruCache() override = default;

    // 添加缓存
    void put(const Key &key, Value value) override
    {
        // 被驱逐（从LRU删除的键）
        Key ignoredEvictedKey{};
        putAndGetEvictedKey(key, value, ignoredEvictedKey);
    }

    // 返回本次是否因插入新结点而发生淘汰
    // 若返回true，evictedKey为被淘汰结点的key
    // 替代addNewNode()
    bool putAndGetEvictedKey(const Key& key,
                             const Value& value,
                             Key& evictedKey)
    {
        // capacity == 0表示禁用缓存
        if (m_capacity == 0) return false;

        std::lock_guard<std::mutex> lock(m_mutex);

        auto it = m_nodeMap.find(key);
        if (it != m_nodeMap.end()) {
            updateExistingNode(it->second, value);
            return false;
        }

        bool evicted = false;
        if (m_nodeMap.size() >= static_cast<size_t>(m_capacity)) {
            evictedKey = evictLeastRecent();
            evicted = true;
        }

        NodePtr newNode = std::make_shared<LruNodeType>(key, value);
        insertNode(newNode);
        m_nodeMap[key] = newNode;

        return evicted;
    }

    bool get(const Key &key, Value &value) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        auto it = m_nodeMap.find(key);
        if (it != m_nodeMap.end()) {
            moveToMostRecent(it->second);
            value = it->second->getValue();
            return true;
        }
        return false;
    }

    Value get(const Key &key) override
    {
        Value value{};
        get(key, value);
        return value;
    }

    void remove(Key key)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        auto it = m_nodeMap.find(key);
        if (it != m_nodeMap.end()) {
            removeNode(it->second);
            m_nodeMap.erase(it);
        }
    }

private:
    static int validateCapacity(int capacity)
    {
        if (capacity < 0) {
            throw std::invalid_argument("LruCache capacity must not be negative");
        }
        return capacity;
    }

    void initializeList()
    {
        // 创建首尾虚拟节点
        m_dummyHead = std::make_shared<LruNodeType>(Key(), Value());
        m_dummyTail = std::make_shared<LruNodeType>(Key(), Value());
        
        m_dummyHead->m_next = m_dummyTail;
        m_dummyTail->m_prev = m_dummyHead;
    }

    void updateExistingNode(NodePtr node, const Value &value)
    {
        node->setValue(value);
        moveToMostRecent(node);
    }


    // void addNewNode(const Key& key, const Value& value)
    // {
    //     if (m_nodeMap.size() >= m_capacity)
    //     {
    //         evictLeastRecent();
    //     }

    //     NodePtr newNode = std::make_shared<LruNodeType>(key, value);
    //     insertNode(newNode);
    //     m_nodeMap[key] = newNode;
    // }

    // 将该结点移动到最新位置
    void moveToMostRecent(NodePtr node)
    {
        removeNode(node);
        insertNode(node);
    }

    // 移除结点
    void removeNode(NodePtr node)
    {
        if (!node->m_prev.expired() && node->m_next)
        {
            NodePtr prev = node->m_prev.lock();
            prev->m_next = node->m_next;
            node->m_next->m_prev = prev;
            node->m_next = nullptr; // 清空_next指针，断开结点
        }
    }

    // 从尾部插入结点
    void insertNode(NodePtr node)
    {
        node->m_next = m_dummyTail;
        node->m_prev = m_dummyTail->m_prev.lock();
        m_dummyTail->m_prev.lock()->m_next = node;
        m_dummyTail->m_prev = node;
    }

    // 驱逐最近最少访问，调用前要保证缓存非空
    Key evictLeastRecent()
    {
        NodePtr leastRecent = m_dummyHead->m_next;
        Key evictedKey = leastRecent->getKey();

        removeNode(leastRecent);
        m_nodeMap.erase(evictedKey);

        return evictedKey;
    }



private:
    int         m_capacity; // 缓存容量
    NodeMap     m_nodeMap; // key -> node
    std::mutex  m_mutex;
    NodePtr     m_dummyHead; // 虚拟头结点
    NodePtr     m_dummyTail; // 虚拟尾结点
};


/*
我在实现 LRU-K 时遇到的难点是：历史访问计数和历史 value 分别存放在两个容器中。
历史计数使用容量受限的 LRU；当历史缓存满时，它会自动淘汰最久未使用的 key。但原先代码无法感知“淘汰了哪个 key”，导致 m_historyValueMap 中对应的 value 无法同步删除，可能形成残留数据并持续占用内存。

我的解决方法：
通过在基础LRU中添加一个返回值类型为bool，可以将key加入缓存，并返回被淘汰key的接口，并在put中调用，返回为true表示有key淘汰并通过引用参数获取被淘汰的key。在LRUK中判断是否有key淘汰，如果有就删除historyValueMap中的key。

我的解决方式是扩展基础 LRU 的写入接口：在插入新 key 时，接口通过 bool 返回值表示是否发生淘汰，并通过引用参数返回被淘汰的 key。LRU-K 更新历史记录后，如果发现发生淘汰，就立即根据该 key 删除 m_historyValueMap 中对应的 value。
这样保证了历史计数和历史 value 的生命周期一致，避免了过期 value 残留和内存增长问题。
*/
template<typename Key, typename Value>
class LruKCache : public LruCache<Key, Value>
{
public:
    LruKCache(int capacity, int historyCapacity, int k)
        : LruCache<Key, Value>(validateMainCapacity(capacity))
        , m_historyList(std::make_unique<LruCache<Key, size_t>>(
                validateHistoryCapacity(historyCapacity)))
        , m_k(validateK(k))
    {}

    Value get(const Key &key) override
    {
        // 首先尝试从主缓存获取数据
        Value value{};
        
        get(key, value);
        
        return value;
    }

    bool get(const Key &key, Value &value) override
    {
        std::lock_guard<std::mutex> lock(m_mutex_k);

        bool inMainCache = LruCache<Key, Value>::get(key, value);

        // 如果数据在主缓存中，直接返回
        if (inMainCache)
        {
            return true;
        }

        // 主缓存未命中，本次get计入历史访问
        const size_t historyCount = recordHistoryAccess(key);

        // 如果数据不在主缓存，但访问次数达到了k次
        if (historyCount >= static_cast<size_t>(m_k))
        {
            // 检查是否有历史值记录
            auto it = m_historyValueMap.find(key);
            if (it != m_historyValueMap.end())
            {
                // 有历史值且>=k，记录并将其添加到主缓存
                value = it->second;

                // 从历史记录移除
                m_historyList->remove(key);
                m_historyValueMap.erase(it);

                // 添加到主缓存
                LruCache<Key, Value>::put(key, value);

                return true;
            }
            // 没有历史值记录，无法添加到缓存，返回默认值
        }
        // 数据不在主缓存且不满足添加条件，返回false
        return false;
    }

    void put(const Key &key, Value value) override
    {
        std::lock_guard<std::mutex> lock(m_mutex_k);

        // 检查是否已在主缓存
        Value existingValue{};

        bool inMainCache = LruCache<Key, Value>::get(key, existingValue);

        if (inMainCache)
        {
            // 已在主缓存，直接更新
            LruCache<Key, Value>::put(key, value);
            return;
        }

        // 缓存未命中，本次 put 计入历史访问
        size_t historyCount = recordHistoryAccess(key);

        // 保存值到历史记录映射，供后续get操作使用
        m_historyValueMap[key] = value;

        // 检查是否到达k次访问阈值
        if (historyCount >= static_cast<size_t>(m_k))
        {
            // 添加到主缓存
            m_historyList->remove(key);
            m_historyValueMap.erase(key);
            LruCache<Key, Value>::put(key, value);
        }
    }

private:
    static int validateMainCapacity(int capacity)
    {
        if (capacity <= 0) {
            throw std::invalid_argument(
                "LruKCache capacity must be greater than 0");
        }

        return capacity;
    }

    static int validateHistoryCapacity(int historyCapacity)
    {
        if (historyCapacity <= 0) {
            throw std::invalid_argument(
                "LruKCache historyCapacity must be greater than 0");
        }

        return historyCapacity;
    }

    static int validateK(int k)
    {
        if (k <= 0) {
            throw std::invalid_argument(
                "LruKCache k must be greater than 0");
        }

        return k;
    }

    // 调用者必须已经持有 _mutex_k。
    size_t recordHistoryAccess(const Key& key)
    {
        size_t historyCount = m_historyList->get(key);
        ++historyCount;

        Key evictedHistoryKey{};
        const bool historyEvicted =
            m_historyList->putAndGetEvictedKey(
                key, historyCount, evictedHistoryKey);

        // 历史访问记录被 LRU 淘汰时，同步清理暂存的 value。
        if (historyEvicted) {
            m_historyValueMap.erase(evictedHistoryKey);
        }

        return historyCount;
    }

    int m_k; // 进入缓存队列的评判标准
    std::unique_ptr<LruCache<Key, size_t>> m_historyList; // 访问数据历史记录（value为访问次数）
    std::unordered_map<Key, Value> m_historyValueMap; // 存储未达到k次访问的数据值 
    std::mutex m_mutex_k;
};


// 使用组合实现 支持默认 TTL 和每项自定义 TTL、读取时不续期的绝对过期 LRU。（Time-To-Live 允许存活的时长）
template<typename Value>
class ValueWithTTL
{
public:
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::milliseconds;
    
    ValueWithTTL() = default;

    ValueWithTTL(const Value& value, Clock::time_point expireTime, Duration ttl)
                : m_value(value), m_expireTime(expireTime), m_ttl(ttl)
    {}

    Value GetValue() const
    {
        return m_value;
    }

    Clock::time_point GetexpireTime() const
    {
        return m_expireTime;
    }

    Duration GetTTL() const
    {
        return m_ttl;
    }

private:
    Value m_value{};

    // 默认对象视为已经过期
    Clock::time_point m_expireTime{
        Clock::time_point::min()
    };

    Duration m_ttl{Duration::zero()};
};

template<typename Key, typename Value>
class LruCacheWithTTL : public ICachePolicy<Key, Value>
{

public:
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::milliseconds;

    // LruCacheWithTTL() = default;

    explicit LruCacheWithTTL(int capacity
        , Duration defaultTTL = Duration{10})
        : m_cache(capacity) 
        , m_defaultTTL(validateTTL(defaultTTL))
    {}

    ~LruCacheWithTTL() override = default;

    void put(const Key &key, const Value value) override
    {
        std::lock_guard<std::mutex> lock(m_ttl_mutex);

        Duration ttl = m_defaultTTL;
        ValueWithTTL<Value> oldValueWithTTL;

        if (m_cache.get(key, oldValueWithTTL)) {
            const auto now = Clock::now();

            if (now < oldValueWithTTL.GetexpireTime()) {
                ttl = oldValueWithTTL.GetTTL();
            }
            else {
                m_cache.remove(key);
            }
        }

        putUnlocked(key, value, ttl);
    }

    void put(const Key &key, const Value &value, Duration ttl) 
    {
        std::lock_guard<std::mutex> lock(m_ttl_mutex);

        ttl = validateTTL(ttl);

        //const auto expireTime = Clock::now() + ttl;

        putUnlocked(key, value, ttl);
    }

    bool get(const Key &key, Value &value) override
    {
        std::lock_guard<std::mutex> lock(m_ttl_mutex);

        return getUnlocked(key, value, false);
    }

    bool getAndRefreshTTL(const Key &key, Value &value)
    {
        std::lock_guard<std::mutex> lock(m_ttl_mutex);

        return getUnlocked(key, value, true);
    }

    Value get(const Key &key) override
    {
        Value value{};
        get(key, value);
        return value;
    }

    void remove(const Key &key)
    {
        std::lock_guard<std::mutex> lock(m_ttl_mutex);
        m_cache.remove(key);
    }

private:
    static Duration validateTTL(Duration ttl)
    {
        if (ttl <= Duration::zero()) {
            throw std::invalid_argument("TTL must be greater than zero");
        }

        return ttl;
    }

    bool getUnlocked(const Key &key, Value &value, bool refreshTTL)
    {
        ValueWithTTL<Value> v;
        
        if (!m_cache.get(key, v)) return false;

        const auto now = Clock::now();

        if (now >= v.GetexpireTime()) {
            m_cache.remove(key);
            return false;
        }

        value = v.GetValue();

        if (refreshTTL) {
            const Duration ttl = v.GetTTL();
            const auto newExpireTime = now + ttl;

            m_cache.put(key, ValueWithTTL<Value>{
                            value,
                            newExpireTime,
                            ttl
            });
        }
        
        return true;
    }

    void putUnlocked(const Key& key, const Value &value, Duration ttl) {
        // const auto now = Clock::now();
        // const auto newExpireTime = now + ttl;

        m_cache.put(key, ValueWithTTL<Value>{
                        value,
                        Clock::now() + ttl,
                        ttl
        });
    }

private:
    LruCache<Key, ValueWithTTL<Value>> m_cache;
    std::mutex m_ttl_mutex;
    Duration m_defaultTTL;

};




} // namesapce CppCache