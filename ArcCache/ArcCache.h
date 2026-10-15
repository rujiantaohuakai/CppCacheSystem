#pragma once

#include "../ICachePolicy.h"
#include "ArcLruPart.h"
#include "ArcLfuPart.h"
#include <memory>
#include <mutex>


namespace CppCache {

template<typename Key, typename Value>
class ArcCache : public ICachePolicy<Key, Value>
{
private:
    size_t m_capacity;
    size_t m_transformThreshold;
    std::mutex m_mutex;
    std::unique_ptr<ArcLruPart<Key, Value>> m_lruPart;
    std::unique_ptr<ArcLfuPart<Key, Value>> m_lfuPart;


public:
    explicit ArcCache(size_t capacity = 10, size_t transformThreshold = 4)
    : m_capacity(capacity)
    , m_transformThreshold(transformThreshold)
    , m_lruPart(std::make_unique<ArcLruPart<Key, Value>>(capacity, transformThreshold))
    , m_lfuPart(std::make_unique<ArcLfuPart<Key, Value>>(capacity, transformThreshold))
    {}

    ~ArcCache() override = default;

    // 缓存接口
    void put(const Key &key, Value value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        checkGhostCaches(key);

        // 检查LFU部分是否存在该键
        bool inLfu = m_lfuPart->contain(key);
        // 更新LRU部分
        m_lruPart->put(key, value);
        // 如果LFU部分存在，则更新
        if (inLfu) {
            m_lfuPart->put(key, value);
        }

    }
    // 传入key，访问到的值以传出参数形式返回。访问成功返回true
    bool get(const Key &key, Value &value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        checkGhostCaches(key);

        bool shouldTransform = false;
        if (m_lruPart->get(key, value, shouldTransform)) {
            if (shouldTransform) {
                m_lfuPart->put(key, value);
            }
            return true;
        }
        return m_lfuPart->get(key, value);
    }
    // 缓存中有key，返回value
    Value get(const Key &key) {
        Value value{};
        get(key, value);
        return value;
    }
private:

    bool checkGhostCaches(Key key) {
        bool inGhost = false;

        if (m_lruPart->checkGhost(key)) {
            // 在lru幽灵缓存中，减少lfu容量，增加lru容量
            if (m_lfuPart->decreaseCapacity()) {
                m_lruPart->increaseCapacity();
            }
            inGhost = true;
        }
        else if (m_lfuPart->checkGhost(key)) {
            if (m_lruPart->decreaseCapacity()) {
                m_lfuPart->increaseCapacity();
            }
            inGhost = true;
        }
        return inGhost;
    }

};


    
} // namesapce CppCache