# CppCacheSystem

基于 **C++17** 实现的进程内键值缓存项目，开发环境为 **Ubuntu 24.04**，使用 **CMake** 构建。项目通过统一的模板接口实现多种缓存淘汰与准入策略，并提供过期时间管理、访问频率老化及模拟负载下的命中率对比。

适合用于学习缓存算法、数据结构组织、对象生命周期管理和互斥同步，也可作为应用内缓存组件的实验基础。

## 功能概览

| 策略 | 类型 | 实现要点 |
| --- | --- | --- |
| LRU | `LruCache<Key, Value>` | 哈希表定位节点，双向链表维护访问顺序，淘汰最久未使用的数据 |
| LFU | `LfuCache<Key, Value>` | 哈希表与频率分桶双向链表，优先淘汰低频数据，常规访问按最近访问顺序维护同频节点 |
| LFU-Aging | `LfuCache<Key, Value>` 的参数配置 | 平均访问频次超过阈值时统一降低节点频率，减弱历史高频数据的长期影响 |
| LRU-K | `LruKCache<Key, Value>` | 历史访问计数达到 K 后进入主 LRU；历史计数淘汰时同步清理暂存值 |
| LRU-TTL | `LruCacheWithTTL<Key, Value>` | 组合 LRU，支持默认 TTL、单项 TTL、访问时惰性删除及显式续期 |
| ARC 思想的混合缓存 | `ArcCache<Key, Value>` | 组合 LRU/LFU 分区，根据幽灵记录命中调整分区容量，将多次访问的数据加入 LFU 分区 |

公共操作使用 `std::mutex` 和 `std::lock_guard` 保护缓存状态；链表节点通过 `shared_ptr` 管理后继、通过 `weak_ptr` 引用前驱，避免双向强引用形成环。当前采用实例级互斥，同一缓存实例的读写操作会串行执行。

> `ArcCache` 是基于 ARC 思想的 LRU/LFU 混合实现。其分区容量、节点存储和迁移方式见下文，实现与标准 ARC 存在差别。

## 项目结构

```text
CppCacheSystem/
├── CMakeLists.txt          # CMake 配置，生成 main 可执行文件
├── ICachePolicy.h          # 统一缓存接口
├── LruCache.h              # LRU、LRU-K、TTL 值封装及 LRU-TTL
├── LfuCache.h              # LFU 与访问频率老化
├── ArcCache/
│   ├── ArcCache.h          # 混合策略及分区容量调整
│   ├── ArcNode.h           # 缓存节点
│   ├── ArcLruPart.h        # LRU 分区和幽灵记录
│   └── ArcLfuPart.h        # LFU 分区和幽灵记录
└── testCachePolicy.cpp     # 命中率对比及 TTL 功能测试
```

缓存实现位于头文件中，接入时包含对应头文件即可；当前 CMake 目标用于运行测试程序。

## 构建与运行

### Ubuntu 24.04

需要支持 C++17 的编译器和 CMake 3.10 或更高版本，无额外第三方库依赖。

```bash
sudo apt update
sudo apt install -y build-essential cmake
```

在包含 `CMakeLists.txt` 的项目根目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/main
```

跨系统拷贝项目时，请使用新的构建目录，避免复用其他系统生成的 `CMakeCache.txt`。例如，将上述命令中的 `build` 统一替换为 `build-ubuntu`。

程序依次输出三组负载的命中率和两组 TTL 功能测试结果。TTL 测试包含主动等待，仅等待时间就约 12 秒，完整运行时间还取决于机器性能。

### 直接编译

也可以在项目根目录直接使用 GCC：

```bash
g++ -std=c++17 -O2 -pthread testCachePolicy.cpp -o main
./main
```

## 使用示例

### LRU 与统一接口

```cpp
#include <iostream>
#include <string>
#include "LruCache.h"

int main() {
    CppCache::LruCache<int, std::string> cache(2);
    CppCache::ICachePolicy<int, std::string>& policy = cache;

    policy.put(1, "one");
    policy.put(2, "two");

    std::string value;
    if (policy.get(1, value)) {
        std::cout << value << '\n'; // one，键 1 成为最近访问的数据
    }

    policy.put(3, "three");         // 容量已满，淘汰键 2
    std::cout << std::boolalpha
              << policy.get(2, value) << '\n'; // false
}
```

所有策略均实现以下公共接口，位于 `CppCache` 命名空间：

| 接口 | 行为 |
| --- | --- |
| `void put(const Key& key, Value value)` | 按具体策略插入或更新数据 |
| `bool get(const Key& key, Value& value)` | 命中返回 `true` 并写入输出参数，未命中返回 `false` |
| `Value get(const Key& key)` | 命中返回值，未命中返回 `Value{}` |

建议使用返回 `bool` 的 `get`，以区分未命中和缓存中存储的默认值。键需要支持哈希与相等比较；当前实现还要求键和值可默认构造、可复制。

### 切换策略

```cpp
#include <string>
#include "LruCache.h"
#include "LfuCache.h"
#include "ArcCache/ArcCache.h"

CppCache::LruCache<int, std::string> lru(100);
CppCache::LfuCache<int, std::string> lfu(100);
CppCache::LfuCache<int, std::string> lfuAging(100, 100); // 平均频次阈值
CppCache::LruKCache<int, std::string> lruK(100, 500, 2); // 主容量、历史容量、K
CppCache::ArcCache<int, std::string> arc(100, 4);       // 分区初始容量、频次门槛
```

### TTL 与显式续期

```cpp
#include <chrono>
#include <string>
#include "LruCache.h"

using namespace std::chrono_literals;

CppCache::LruCacheWithTTL<int, std::string> cache(100, 5s);
cache.put(1, "default");       // 新键使用默认 TTL：5 秒
cache.put(2, "custom", 10s);   // 此键使用 10 秒 TTL

std::string value;
bool hit = cache.get(2, value);                // 普通读取不续期
bool refreshed = cache.getAndRefreshTTL(2, value); // 有效时读取并续期 10 秒

cache.put(2, "updated");      // 有效键沿用 10 秒 TTL，从本次写入重新计时
cache.remove(1);
```

TTL 使用 `std::chrono::steady_clock` 记录过期时刻，时长类型为 `std::chrono::milliseconds`。不显式传入默认 TTL 时，构造函数默认值为 **10 毫秒**，接入时建议根据业务需要设置。

| 操作或状态 | TTL 语义 |
| --- | --- |
| 新键调用 `put(key, value)` | 使用默认 TTL |
| 未过期键调用 `put(key, value)` | 沿用该键的 TTL 时长，更新值并重新计算过期时刻 |
| 已过期键调用 `put(key, value)` | 视为新键，使用默认 TTL |
| 调用 `put(key, value, ttl)` | 使用显式 TTL，覆盖原 TTL 并重新计时 |
| 调用 `get(key, value)` | 检查过期，读取不续期；过期则删除并返回 `false` |
| 调用 `getAndRefreshTTL(key, value)` | 仅有效数据可以读取并续期，已过期数据无法通过续期复活 |

TTL 必须大于零。当前采用惰性过期，没有后台清理线程；未被访问的过期项仍可能占用容量，直到被淘汰或删除。

## 策略实现说明

### LRU 与 LRU-K

LRU 通过 `unordered_map` 实现节点定位，通过带首尾哨兵的双向链表维护访问顺序。读命中和更新都会将节点移动到最近访问端，满容量时从另一端淘汰。

LRU-K 采用主 LRU、历史计数 LRU 与历史值映射。主缓存未命中时，`get` 和 `put` 都会累计历史访问次数；达到 K 且存在暂存值后，数据进入主缓存。未达到门槛时，即使之前写入过值，`get` 也可能返回 `false`。该实现采用访问次数准入，未维护第 K 次历史访问时间戳。

基础 LRU 提供 `putAndGetEvictedKey`，返回是否发生淘汰及被淘汰的键。LRU-K 利用该接口同步删除历史值映射中的对应项，使历史计数和暂存值保持一致，避免历史记录淘汰后留下无效值。

### LFU 与频率老化

LFU 将节点组织到不同频率的双向链表中，并维护最小频率。命中读取或更新已有键时，节点进入更高频率桶；常规访问按最近访问顺序维护同频节点。老化时会按哈希表遍历顺序重新组织节点，合并后的频率桶不保证保留原有访问次序。

`LfuCache(capacity, maxAverageNum)` 可配置平均频率阈值。当平均频率超过阈值时，遍历节点，将频率减少 `max(1, maxAverageNum / 2)`，最低保留为 1，随后重新统计总频率和最小频率。单参数构造的阈值默认为 `100000000`，对比测试中的 LFU-Aging 使用阈值 `100`。

### ARC 思想的混合策略

新数据写入 LRU 分区；LRU 命中的节点达到访问频次门槛后，也会加入 LFU 分区。淘汰节点进入相应分区的幽灵记录，后续访问命中幽灵记录时，尝试缩小另一分区并扩大本分区。

当前实现有以下容量与存储语义：

- 构造参数为 `C` 时，LRU 与 LFU 主分区的初始容量各为 `C`，主分区容量之和为 `2C`；同一键可能同时存在于两个分区。
- 加入 LFU 后不会立即从 LRU 删除，属于数据复制而非独占迁移。
- 幽灵记录保留完整节点，包含值；两个幽灵区的容量初始也各为 `C`。
- 分区缩容后至少保留 1 个容量；上述主区与幽灵区均存在额外存储开销。

因此，测试输出中的相同 `CAPACITY` 参数不代表 ARC 与其他策略使用相同的总存储预算。

### 操作复杂度

在忽略键值复制成本、互斥等待且哈希表平均查找为 O(1) 的情况下：

| 策略 | 常规操作 | 额外成本 |
| --- | --- | --- |
| LRU | `get` / `put` 平均 O(1) | 链表调整和单次淘汰均为 O(1) |
| LRU-K | 常规 `get` / `put` 平均 O(1) | 维护主缓存之外的历史计数与暂存值 |
| LRU-TTL | `get` / `put` 平均 O(1) | 时间检查、惰性删除或显式续期 |
| LFU | 未触发老化时，`get` / `put` 平均 O(1) | 老化需遍历节点与频率桶 |
| ARC 混合策略 | 取决于访问的分区和是否调整容量 | LFU 分区使用 `std::list::remove` 查找节点，可能遍历同频率桶；更新最小频率也可能遍历频率桶 |

## 测试与结果解读

`testCachePolicy.cpp` 对比 LRU、LFU、ARC、LRU-K、LRU-TTL 和 LFU-Aging 六种配置：

| 场景 | 容量参数 | 每种配置的读写操作数，不含预热 | 负载组成 |
| --- | --- | --- | --- |
| 热点访问 | 20 | 500,000 | 约 70% 读、30% 写；约 70% 访问 20 个热点键，其余访问 5,000 个冷键 |
| 循环扫描 | 50 | 200,000 | 约 80% 读、20% 写；60% 顺序扫描、30% 随机跳跃、10% 范围外访问 |
| 工作负载变化 | 30 | 80,000 | 五阶段切换：热点、大范围随机、顺序扫描、局部访问、混合访问 |

每个场景开始测试各策略时，都会将随机引擎重置为种子 `1024`，保证同一场景使用相同的操作序列。三组负载合计每种配置 **780,000 次**读写操作，六种配置合计 **4,680,000 次**，均不含预热。

输出指标为命中次数、读取次数和命中率：

```text
命中率 = 成功读取次数 / 总读取次数 × 100%
```

此外，程序包含 **11 项 TTL 功能检查**和 **9 项 TTL 写入语义检查**，覆盖默认/自定义 TTL、普通读取不续期、显式续期、更新值后重新计时、主动删除和过期键重新写入等行为。

解读结果时需要结合实现：

- ARC 的总容量预算与其他策略不同；LRU-K 还保存额外历史计数和暂存值，不能仅用主容量比较内存效率。
- LRU-TTL 在负载对比中使用默认 10 毫秒 TTL，命中率受实际执行速度和线程调度影响，即使操作序列相同也可能波动。
- 当前负载测试为单线程，统计命中率，没有输出吞吐量、延迟或并发扩展性指标；读取未命中时也没有自动从后端加载数据。
- 当前 `main` 始终返回 0，TTL 检查失败不会反映为非零退出码。运行后需查看 `[FAIL]` 和两组通过数；当前没有注册 CTest 测试。

## 当前边界与后续方向

项目聚焦进程内缓存算法，当前不包含持久化、网络服务、分布式一致性或自动回源加载。容量按条目数计算，并非字节级内存上限。

后续可在统一总存储预算后完善策略对比，引入自动失败退出码、淘汰顺序与历史清理专项测试、多线程压力测试，以及吞吐量和延迟统计；也可以增加分片锁、过期项主动清理和更严格的 ARC 容量管理。
