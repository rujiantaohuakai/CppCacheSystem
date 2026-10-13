#pragma once

#include <memory>

namespace CppCache {

template<typename Key, typename Value>
class ArcNode {
private:
    Key m_key;
    Value m_value;
    std::weak_ptr<ArcNode> m_prev;
    std::shared_ptr<ArcNode> m_next;

public:
    ArcNode() 
    : m_next(nullptr)
    {}

    ArcNode(Key key, Value value)
    : m_key(key), m_value(value), m_next(nullptr);
    {}

    // 访问器
    Key getKey() const {return m_key;}
    Value getValue() const {return m_value;}
    
    void setValue(const Value &value) {
        m_value = value;
    }

    friend class ArcLruPart<Key, Value>
    friend class ArcLfuPart<Key, Value>
}



















}