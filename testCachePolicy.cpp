#include <iostream>
#include <string>
#include <chrono>
#include <vector>
#include <iomanip>
#include <random>
#include <algorithm>
#include <array>
#include <thread>
#include "ICachePolicy.h"
#include "LruCache.h"

// 计时器
class Timer {
public :
    Timer() : _start(std::chrono::high_resolution_clock::now()) {}

    double elapsed() {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - _start).count();
    }
    
private:
    std::chrono::time_point<std::chrono::high_resolution_clock> _start;
};

void printResults(const std::string& testName, int capacity,
                    const std::vector<int>& get_operations,
                    const std::vector<int>& hits) {
    
    std::cout << "=====" << testName << " 结果汇总 =====" << std::endl;
    std::cout << "缓存大小: " << capacity << std::endl;

    std::vector<std::string> names = {"LRU", "LFU", "ARC", "LRU-K", "LRU-TTL", "LFU-Aging"};

    for (size_t i = 0; i < hits.size(); ++i) {
        double hitRate = 100.0 * hits[i] / get_operations[i];
        std::cout << (i < names.size() ? names[i] : "Algorithm " + std::to_string(i + 1))
                  << " - 命中率: " << std::fixed << std::setprecision(2)
                  << hitRate << "% ";

        //添加具体命中次数和总操作次数
        std::cout << "(" << hits[i] << "/" << get_operations[i] << ")" << std::endl;
    }

    std::cout << std::endl;

}

void testHotDataAccess() {
    std::cout << "\n=== 测试场景1：热点数据访问测试 ===" << std::endl;
    
    const int CAPACITY = 20;            // 缓存容量
    const int OPERATIONS = 500000;      // 总操作次数
    const int HOT_KEYS = 20;            // 热点数据数量
    const int COLD_KEYS = 5000;         // 冷数据数量

    CppCache::LruCache<int, std::string> lru(CAPACITY);
    // LRU-K参数：
    // 主缓存容量不变
    // 历史记录容量设为可能访问的所有键数量
    // k = 2 数据被访问2次后才进入缓存，区分热点和冷数据
    CppCache::LruKCache<int, std::string> lruk(CAPACITY, HOT_KEYS + COLD_KEYS, 2);

    // LRU-TTL
    CppCache::LruCacheWithTTL<int, std::string> lruttl(CAPACITY);

    std::random_device rd;
    std::mt19937 gen(rd());

    std::array<CppCache::ICachePolicy<int, std::string>*, 6> caches = {&lru, &lru, &lru, &lruk, &lruttl, &lru};
    std::vector<int> hits(6, 0);
    std::vector<int> get_operations(6, 0);
    std::vector<std::string> names = {"LRU", "LFU", "ARC", "LRU-K", "LRU-TTL", "LFU-Aging"};

    // 为所有缓存对象进行相同的操作序列测试
    for (int i = 0; i < caches.size(); ++i) {
        // 预热缓存，插入一些数据
        for (int key = 0; key < HOT_KEYS; ++key) {
            std::string value = "value" + std::to_string(key);
            caches[i]->put(key, value);
        }

        // 交替进行put和get操作，模拟真实场景
        for (int op = 0; op < OPERATIONS; ++op) {
            // 设置30%概率进行写操作，70%概率进行读操作
            bool isPut = (gen() % 100 < 30);
            int key;

            // 70%概率访问热点数据，30%概率访问冷数据
            if (gen() % 100 < 70) {
                key = gen() % HOT_KEYS;
            }
            else {
                key = HOT_KEYS + (gen() % COLD_KEYS);
            }

            if (isPut) {
                std::string value = "value" + std::to_string(key) + "_v" + std::to_string(op % 100);
                caches[i]->put(key, value);
            }
            else {
                // 执行get操作并记录命中情况
                std::string result;
                get_operations[i]++;
                if (caches[i]->get(key, result)) {
                    hits[i]++;
                }
            }
        }
    }

    // 打印测试结果
    printResults("热点数据访问测试", CAPACITY, get_operations, hits);
}

void testLoopPattern() {
    std::cout << "\n=== 测试场景2：循环扫描测试 ===" << std::endl;

    const int CAPACITY = 50;          // 缓存容量
    const int LOOP_SIZE = 500;        // 循环范围大小
    const int OPERATIONS = 200000;    // 总操作次数

    CppCache::LruCache<int, std::string> lru(CAPACITY);
    // LRU-K参数：
    // 历史记录容量为总循环大小两倍，覆盖范围内和范围外的数据
    // k = 2
    CppCache::LruKCache<int, std::string> lruk(CAPACITY, LOOP_SIZE * 2, 2);

    // LRU-TTL
    CppCache::LruCacheWithTTL<int, std::string> lruttl(CAPACITY);

    std::array<CppCache::ICachePolicy<int, std::string>*, 6> caches = {&lru, &lru, &lru, &lruk, &lruttl, &lru};
    std::vector<int> hits(6, 0);
    std::vector<int> get_operations(6, 0);
    std::vector<std::string> names = {"LRU", "LFU", "ARC", "LRU-K", "LRU-TTL", "LFU-Aging"};

    std::random_device rd;
    std::mt19937 gen(rd());

    // 为每种缓存算法运行相同的测试
    for (int i = 0; i < caches.size(); ++i) {
        // 先预热一部分数据（只加载20%的数据）
        for (int key = 0; key < LOOP_SIZE / 5; ++key) {
            std::string value = "loop" + std::to_string(key);
            caches[i]->put(key, value);
        }

        // 设置循环扫描的当前位置
        int current_pos = 0;

        // 交替进行读写操作，模拟真实场景
        for (int op = 0; op < OPERATIONS; ++op) {
            // 20%概率是写操作，80%概率是读操作
            bool isPut = (gen() % 100 < 20);
            int key;

            // 按照不同模式选择键
            if (op % 100 < 60) { // 60%顺序扫描
                key = current_pos;
                current_pos = (current_pos + 1) % LOOP_SIZE;
            }
            else if (op % 100 < 90){ // 30%随机跳跃
                key = gen() % LOOP_SIZE;
            }
            else { // 10%访问范围外数据
                key = LOOP_SIZE + (gen() % LOOP_SIZE);
            }

            if (isPut) {
                // 执行put操作，更新数据
                std::string value = "loop" + std::to_string(key) + "_v" + std::to_string(op % 100);
                caches[i]->put(key, value);
            }
            else {
                // 执行get操作并记录命中情况
                std::string result;
                get_operations[i]++;
                if (caches[i]->get(key, result)) {
                    hits[i]++;
                }
            }
        }
    }

    printResults("循环扫描测试", CAPACITY, get_operations, hits);
}

void testWorkloadShift() {
    std::cout << "\n=== 测试场景3：工作负载剧烈变化测试 ===" << std::endl;

    const int CAPACITY = 30;            // 缓存容量
    const int OPERATIONS = 80000;       // 总操作次数
    const int PHASE_LENGTH = OPERATIONS / 5;  // 每个阶段的长度
    
    CppCache::LruCache<int, std::string> lru(CAPACITY);
    // LRU-K参数：
    // 历史记录容量500， k = 2
    CppCache::LruKCache<int, std::string> lruk(CAPACITY, 500, 2);

    // LRU-TTL
    CppCache::LruCacheWithTTL<int, std::string> lruttl(CAPACITY);

    std::random_device rd;
    std::mt19937 gen(rd());

    std::array<CppCache::ICachePolicy<int, std::string>*, 6> caches = {&lru, &lru, &lru, &lru, &lruttl, &lru};
    std::vector<int> hits(6, 0);
    std::vector<int> get_operations(6, 0);
    std::vector<std::string> names = {"LRU", "LFU", "ARC", "LRU-K", "LRU-TTL", "LFU-Aging"};

    // 为每种缓存算法运行相同的测试
    for (int i = 0; i < caches.size(); ++i) {
        // 先预热缓存，只插入少量初始数据
        for (int key = 0; key < 30; ++key) {
            std::string value = "init" + std::to_string(key);
            caches[i]->put(key, value);
        }

        // 进行多阶段测试，每个阶段都有不同的访问模式
        for (int op = 0; op < OPERATIONS; ++op) {
            // 确定当前阶段
            int phase = op / PHASE_LENGTH;

            // 每个阶段的读写比例不同
            int putProbability;
            switch (phase) {
                case 0: putProbability = 15; break;  // 阶段1: 热点访问，15%写入
                case 1: putProbability = 30; break;  // 阶段2: 大范围随机，30%写入
                case 2: putProbability = 10; break;  // 阶段3: 顺序扫描，10%写入
                case 3: putProbability = 25; break;  // 阶段4: 局部性随机，25%写入
                case 4: putProbability = 20; break;  // 阶段5: 混合访问，20%写入
                default: putProbability = 20;
            }

            bool isPut = (gen() % 100 < putProbability);

            // 根据不同阶段选择不同的访问模式生成key
            int key;
            if (op < PHASE_LENGTH) { // 阶段1: 热点访问 热点数量5
                key = gen() % 5;
            }
            else if (op < PHASE_LENGTH * 2) { // 阶段2: 大范围随机 范围400
                key = gen() % 400;
            }
            else if (op < PHASE_LENGTH * 3) { // 阶段3: 顺序扫描 100个键
                key = (op - PHASE_LENGTH * 2) % 100;
            }
            else if (op < PHASE_LENGTH * 4) { // 阶段4: 局部性随机
                // 产生5个局部区域，每个区域大小为15个键，与缓存大小20接近但略小
                int locality = (op / 800) % 5; // 5个局部区域
                key = locality * 15 + (gen() % 15); // 每个区域15个键
            }
            else { // 阶段5: 混合访问
                int r = gen() % 100;
                if (r < 40) { // 40%概率访问热点
                    key = gen() % 5;
                }
                else if (r < 70) { // 30%概率访问中等范围
                    key = 5 + (gen() % 45);
                }
                else { // 30%概率访问大范围
                    key = 50 + (gen() % 350);
                }
            }

            if (isPut) {
                // 执行写操作
                std::string value = "value" + std::to_string(key) + "_p" + std::to_string(phase);
                caches[i]->put(key, value);
            }
            else {
                // 执行读操作并记录命中情况
                std::string result;
                get_operations[i]++;
                if (caches[i]->get(key, result)) {
                    hits[i]++;
                }
            }
        }
    }

    printResults("工作负载剧烈变化测试", CAPACITY, get_operations, hits);
}

void testLruCacheWithTTL()
{
    std::cout << "\n=== LRU-TTL 功能测试 ===" << std::endl;

    using Cache =
        CppCache::LruCacheWithTTL<int, std::string>;
    using Milliseconds = std::chrono::milliseconds;

    int passed = 0;
    int total = 0;

    auto check = [&](bool condition,
                     const std::string& testName)
    {
        ++total;

        if (condition) {
            ++passed;
            std::cout << "[PASS] " << testName << std::endl;
        }
        else {
            std::cout << "[FAIL] " << testName << std::endl;
        }
    };

    // 功能测试使用500毫秒作为默认TTL
    Cache cache(3, Milliseconds{500});

    CppCache::ICachePolicy<int, std::string>* policy =
        &cache;

    std::string result;

    // 1. 默认TTL：立即读取
    policy->put(1, "default-ttl");

    check(
        policy->get(1, result) &&
        result == "default-ttl",
        "默认TTL：写入后立即读取成功");

    // 2. 默认TTL：过期
    std::this_thread::sleep_for(Milliseconds{600});

    check(
        !policy->get(1, result),
        "默认TTL：500毫秒后缓存过期");

    // 3. 自定义TTL：普通get不刷新TTL
    cache.put(2, "custom-ttl", Milliseconds{1000});

    std::this_thread::sleep_for(Milliseconds{200});

    check(
        policy->get(2, result) &&
        result == "custom-ttl",
        "自定义TTL：200毫秒后仍然有效");

    /*
     * 普通get没有刷新TTL。
     * 从put开始累计等待1100毫秒，已经超过1000毫秒。
     */
    std::this_thread::sleep_for(Milliseconds{900});

    check(
        !policy->get(2, result),
        "普通get不刷新自定义TTL");

    // 4. 重新put刷新TTL和值
    cache.put(3, "old-value", Milliseconds{500});

    std::this_thread::sleep_for(Milliseconds{200});

    cache.put(3, "new-value", Milliseconds{500});

    std::this_thread::sleep_for(Milliseconds{200});

    check(
        cache.get(3, result) &&
        result == "new-value",
        "重新put后刷新TTL并返回新值");

    std::this_thread::sleep_for(Milliseconds{400});

    check(
        !cache.get(3, result),
        "从最后一次put开始计算过期时间");

    // 5. 主动删除
    cache.put(4, "remove-test");
    cache.remove(4);

    check(
        !cache.get(4, result),
        "remove后无法获取");

    /*
     * 6. getAndRefreshTTL测试。
     *
     * 初始TTL为800毫秒。
     */
    cache.put(5, "refresh-ttl", Milliseconds{800});

    std::this_thread::sleep_for(Milliseconds{500});

    check(
        cache.getAndRefreshTTL(5, result) &&
        result == "refresh-ttl",
        "getAndRefreshTTL读取成功并刷新TTL");

    /*
     * 刷新后又经过500毫秒，小于完整的800毫秒，
     * 所以缓存仍然有效。
     */
    std::this_thread::sleep_for(Milliseconds{500});

    check(
        cache.get(5, result) &&
        result == "refresh-ttl",
        "刷新TTL后500毫秒仍然有效");

    /*
     * 上一次调用的是普通get，不会再次刷新。
     * 距离getAndRefreshTTL已经累计经过900毫秒，
     * 超过800毫秒。
     */
    std::this_thread::sleep_for(Milliseconds{400});

    check(
        !cache.get(5, result),
        "刷新后的TTL到期，普通get没有再次续期");

    // 7. 已过期的数据不能通过刷新接口复活
    cache.put(6, "expired-value", Milliseconds{300});

    std::this_thread::sleep_for(Milliseconds{400});

    check(
        !cache.getAndRefreshTTL(6, result),
        "已过期数据不能通过getAndRefreshTTL复活");

    std::cout
        << "LRU-TTL测试结果："
        << passed << "/" << total
        << " 通过"
        << std::endl;
}

int main() {
    testHotDataAccess();
    testLoopPattern();
    testWorkloadShift();
    testLruCacheWithTTL();
    return 0;
}

