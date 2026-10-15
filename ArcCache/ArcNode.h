#pragma once

#include <memory>

namespace CppCache {

template<typename Key, typename Value>
class ArcLruPart;

template<typename Key, typename Value>
class ArcLfuPart;

template<typename Key, typename Value>
class ArcNode {
private:
    Key m_key;
    Value m_value;
    size_t m_freq;
    std::weak_ptr<ArcNode> m_prev;
    std::shared_ptr<ArcNode> m_next;

public:
    ArcNode() 
    : m_next(nullptr)
    {}

    ArcNode(Key key, Value value)
    : m_key(key), m_value(value), m_next(nullptr)
    {}

    // 访问器
    Key getKey() const {return m_key;}
    Value getValue() const {return m_value;}
    size_t getFreq() const {return m_freq;}
    
    void setValue(const Value &value) {
        m_value = value;
    }
    void setFreq(const size_t &freq) {
        m_freq = freq;
    }
    void incrementFrequency() {
        ++m_freq;
    }

    friend class ArcLruPart<Key, Value>;
    friend class ArcLfuPart<Key, Value>;
};









}