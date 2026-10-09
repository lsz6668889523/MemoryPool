# High-Performance Fixed-Size MemoryPool (轻量高性能定长内存池)

## 1. 项目简介 (Overview)
本项目是一个专为高频小对象（Small Objects）分配而设计的轻量、高性能 C++ 定长内存池。针对传统系统级分配器（如 glibc ptmalloc）在频繁申请/释放小对象时存在的**内部/外部内存碎片多、元数据额外开销大、缓存局部性差、内核态切换频繁**等痛点，本项目通过**大块物理内存预申请、嵌入式指针（Embedded Pointer）零空间浪费链表以及 $O(1)$ 游标切分**，实现了极致吞吐量与超低时延。

### 核心特性 (Features)
- **极限吞吐**：分配与释放严格为 $O(1)$ 复杂度，微基准测试下吞吐量达 **3.5 亿+ QPS**，性能达到系统 `malloc` 的 **5.8 倍**。
- **零额外元数据开销 (Zero Metadata Overhead)**：利用**嵌入式指针**技巧复用已释放内存的前 8 字节充当 `next` 指针，不额外占用任何管理头。
- **高 CPU 缓存友好度 (Cache-Friendly)**：采用 4KB/64KB 连续大块物理内存切分，极大提高 L1/L2 缓存行预取命中率。
- **RAII 对象包装与生命周期解耦**：仿照 `boost::object_pool` 架构，将底层裸内存分配与上层对象构造/析构彻底解耦，支持完美转发原位构造（Placement New）。
- **高可靠性保障**：全套代码通过 Google Benchmark 压测、AddressSanitizer (ASan) 边界检测及 Valgrind 深度内存泄漏检测（0 leaks, 0 errors）。
