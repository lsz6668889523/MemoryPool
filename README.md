# High-Performance Fixed-Size MemoryPool (轻量高性能定长内存池)

## 1. 项目简介 (Overview)
本项目是一个专为高频小对象（Small Objects）分配而设计的轻量、高性能 C++ 定长内存池。针对传统系统级分配器（如 glibc ptmalloc）在频繁申请/释放小对象时存在的**内部/外部内存碎片多、元数据额外开销大、缓存局部性差、内核态切换频繁**等痛点，本项目通过**大块物理内存预申请、嵌入式指针（Embedded Pointer）零空间浪费链表以及 $O(1)$ 游标切分**，实现了极致吞吐量与超低时延。

### 核心特性 (Features)
- **极限吞吐**：分配与释放严格为 $O(1)$ 复杂度，微基准测试下吞吐量达 **3.5 亿+ QPS**，性能达到系统 `malloc` 的 **5.8 倍**。
- **零额外元数据开销 (Zero Metadata Overhead)**：利用**嵌入式指针**技巧复用已释放内存的前 8 字节充当 `next` 指针，不额外占用任何管理头。
- **高 CPU 缓存友好度 (Cache-Friendly)**：采用 4KB/64KB 连续大块物理内存切分，极大提高 L1/L2 缓存行预取命中率。
- **RAII 对象包装与生命周期解耦**：仿照 `boost::object_pool` 架构，将底层裸内存分配与上层对象构造/析构彻底解耦，支持完美转发原位构造（Placement New）。
- **高可靠性保障**：全套代码通过 Google Benchmark 压测、AddressSanitizer (ASan) 边界检测及 Valgrind 深度内存泄漏检测（0 leaks, 0 errors）。

---

## 2. 架构设计 (Architecture)

### 2.1 整体分层与物理内存布局图

```text
+-----------------------------------------------------------------------+
|                       MemoryPool (全局池调度器)                         |
|  - freeListHead_: 维护释放后的空闲槽位                                  |
|  - currentBlock_: 维护大内存块物理链表                                  |
+-----------------------------------------------------------------------+
                                   |
         +-------------------------+-------------------------+
         |                                                   |
         v                                                   v
+-------------------------------+           +-------------------------------+
|     MemoryBlock 1 (64 KB)     |           |     MemoryBlock 2 (64 KB)     |
| [BlockHeader] -> next Block   |           | [BlockHeader] -> nullptr      |
+-------------------------------+           +-------------------------------+
| Slot 0 | Slot 1 | Slot 2 |... |           | Slot 0 | Slot 1 | Slot 2 |... |
+-------------------------------+           +-------------------------------+
   |        |
   |        +-----------------------------------+
   |                                            |
   v                                            v
+-----------------------+           +-----------------------+
|  Slot (In-Use 状态)   |           |   Slot (Free 状态)    |
|  [ 用户实际业务数据 ] |           |  [ next 指针 (8B) ]   |  <-- 嵌入式指针 (0 额外开销)
+-----------------------+           +-----------------------+
```

### 2.2 核心设计要点

- **Embedded Pointer (嵌入式指针)**:

  (1) 当 Slot 处于空闲状态时，前 8 字节被解释为 Slot* next，串联在 freeListHead_ 单链表中。
  (2) 当 Slot 分配给用户处于在用状态时，整块内存完全覆盖为用户对象数据。
  (3) 实现每个对象零管理元数据损耗（0 Metadata Overhead）。

- **Chunk Slicing (游标按需切分)**
  (1) 每次向操作系统批量申请大块内存（如 64KB Block），以链表维护。
  (2) 若 FreeList 为空，通过指针游标原子向后步进切割 Slot，避免初始化的无效遍历开销。

- **RAII 生命周期解耦**
  (1) 分配器仅负责裸内存生命周期，析构时统一释放所有申请的 Block，提供兜底防泄漏保证。


## 3. 核心流程时序图 (Sequence Diagrams)

### 3.1 对象分配流程 (Allocate / newElement)

```text
Caller           MemoryPool                FreeList               OS (System)
  |                  |                        |                        |
  |--- newElement()->|                        |                        |
  |                  |--- 检查 freeListHead_->|                        |
  |                  |<-- [命中: 存在空闲槽] -|                        |
  |                  |    弹出头节点返回 (O(1))                          |
  |                  |                                                 |
  |                  |--- [未命中: FreeList 为空]                      |
  |                  |    检查当前 Block 游标...                       |
  |                  |    (若当前 Block 耗尽) ------------------------->|
  |                  |                        向系统申请新 Block (malloc)
  |                  |<------------------------------------------------|
  |                  |    游标步进切分 Slot                                |
  |                  |                                                 |
  |                  |-- Placement New 原地构造 T(args...)              |
  |<-- 返回对象指针 -|                                                 |
```

### 3.2对象释放流程 (Deallocate / deleteElement)

```text
Caller           MemoryPool                FreeList
  |                  |                        |
  |-- deleteElement(p)                        |
  |                  |-- 显式调用 p->~T() 析构|
  |                  |-- 强转 p 为 Slot* ---->|
  |                  |                        |-- 头插法插入 FreeList (O(1))
  |                  |                        |   p->next = freeListHead_
  |                  |                        |   freeListHead_ = p
  |<-- 释放成功 -----|                        |
```

## 4. 性能评测与对比 (Benchmark Results)

### 4.1 测试环境

  - **CPU**:            Intel(R) Core(TM) i5-9300H CPU @ 2.40GHz
  - **OS**:             Ubuntu 22.04.5 LTS x86_64**
  - **Compiler**:       g++ (Ubuntu 12.3.0-1ubuntu1~22.04.3) 12.3.0
  - **Benchmark 工具**: Google Benchmark v1.8.3

### 4.2 量化数据对照表

| 测试项目 | 单次操作延迟<br>(Latency) | 每秒吞吐量 (QPS) | 空间元数据损耗 | 外部碎片 |
| :--- | :--- | :--- | :--- | :--- |
| **glibc malloc/free** | 16.5 ns | 60.5 M items/s | 8 ~ 16 字节 / 对象 | 严重 |
| **Custom MemoryPool** | 2.8 ns | 355.6 M items/s | 0 字节 / 对象 | 0 |
| **性能提升** | 延迟降低 83.0% | 吞吐量提升 5.88 倍 | 内存节省 ~40% | 消除外部碎片 |
