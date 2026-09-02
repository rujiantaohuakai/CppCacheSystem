#pragma once

namespace CppCache
{

template <typename Key, typename Value>
class ICachePolicy
{
public:
    virtual ~ICachePolicy() {};
    // 缓存接口
    virtual void put(const Key &key, Value value) = 0;
    // 传入key，访问到的值以传出参数形式返回。访问成功返回true
    virtual bool get(const Key &key, Value &value) = 0;
    // 缓存中有key，返回value
    virtual Value get(const Key &key) = 0;

};

}