#pragma once
#include <cstddef>
#include <cstdint>
#include <utility>
#include <new>
#include <cassert>
#include <iostream>

// ======================== 1. 嵌入式槽位 (Slot) ========================
// 联合体实现无额外空间浪费的 FreeList
union Slot
{
    Slot* next;// 空闲时作为指针串联
    // 分配后直接覆盖为用户数据区
};

// ======================== 2. 内存块类 (MemoryBlock) =======================
class MemoryBlock
{
private:
    MemoryBlock* next_;         // 指向下一个 Block，用于析构时整链回收
    void* rawMemory_;            // OS 原始申请的内存指针    
    Slot* blockFreeListHead_;   // 该 Block 切分完毕后的 Slots 链表首节点
public:
    // 构造时向 OS 申请一大段连续内存，并完成 Slot 切分串联
    MemoryBlock(size_t blockSize,size_t slotSize,MemoryBlock* next_block);
    ~MemoryBlock()noexcept;

    //获取当前切分好的空闲链表头
    Slot* getFreeListHead()const noexcept{return blockFreeListHead_;}
    MemoryBlock *getNextBlock()const noexcept{return next_;}
};

// ======================== 3. 定长内存池类 (MemoryPool) ========================
template<typename T,size_t BlockSize = 4096>
class MemoryPool
{
private:
    MemoryBlock* blockHead_;
    Slot* freeListHead_;
public:
    // 确保槽位大小至少能放下一个指针，且满足对象对齐
    static constexpr size_t alignment = sizeof(T) > sizeof(Slot)?sizeof(T):sizeof(Slot);   
    static constexpr size_t rawsize = sizeof(T) > sizeof(Slot) ? sizeof(T) : sizeof(Slot);
    static constexpr size_t actualSlotSize = (rawsize + alignment - 1) & ~(alignment - 1);

    MemoryPool() noexcept : blockHead_(nullptr),freeListHead_(nullptr){}
    ~MemoryPool()noexcept
    {
        //级联释放所有申请的 MemoryBlock
        MemoryBlock* curr = blockHead_;
        while (curr)
        {
            MemoryBlock* next = curr->getNextBlock();
            delete curr;
            curr = next;
        }
    }
    //禁止拷贝与移动
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    //分配原始槽位
    void* allocate()
    {
        // 若空闲链表为空，则向 OS 批发新 Block
        if (freeListHead_ == nullptr)
        {
            MemoryBlock* newBlock = new MemoryBlock(BlockSize,actualSlotSize,blockHead_);
            blockHead_ = newBlock;
            freeListHead_ = newBlock->getFreeListHead();
        }
        //从freelisthead弹出一个空闲槽位
        Slot* slot = freeListHead_;
        freeListHead_ = freeListHead_->next;
        return reinterpret_cast<void*>(slot);
    }

    // 回收槽位：O(1) 头插法
    void deallocate(void* ptr)noexcept
    {
        if(!ptr)return;
        Slot* slot = reinterpret_cast<Slot*>(ptr);
        slot->next = freeListHead_;
        freeListHead_  = slot;
    }

    //支持任意构造参数的原地构造(Placement New)
    template <typename... Args>
    T* newElement(Args... args)
    {
        void* mem = allocate();
        try {
            return new (mem) T(std::forward<Args>(args)...);
        } catch (...) {
            deallocate(mem);
            throw;
        }
    }

    //显式析构并归还槽位
    void deleteElement(T* ptr)noexcept
    {
        if(ptr)
        {
            ptr->~T();
            deallocate(ptr);
        }
    }
};



MemoryBlock::MemoryBlock(size_t blockSize,size_t slotSize,MemoryBlock* next_block=nullptr)
    :next_(next_block),rawMemory_(nullptr)
{
    // 向系统批发大内存
    rawMemory_ = ::operator new(blockSize);

    //首地址按照slot尺寸或者系统指针严格对齐
    uintptr_t rawAddr = reinterpret_cast<uintptr_t>(rawMemory_);
    size_t alignment = alignof(std::max_align_t);
    uintptr_t bodyAddr = (rawAddr+alignment-1) & ~(alignment-1);

    char* slotPtr = reinterpret_cast<char*>(bodyAddr);
    size_t availableBytes = (rawAddr + blockSize)-bodyAddr;
    size_t totalSlots = availableBytes/slotSize;

    assert(totalSlots > 0 && "BlockSize is too small for even one slot!");

    // 核心：在内存块内直接切分，并用嵌入式指针串成单链表
    blockFreeListHead_  = reinterpret_cast<Slot*>(slotPtr);
    Slot* curr = blockFreeListHead_;

    for (size_t i = 0; i < totalSlots-1; i++)
    {
        slotPtr +=slotSize;
        curr->next = reinterpret_cast<Slot*>(slotPtr);
        curr = curr->next;
    }
    curr->next = nullptr;
}

MemoryBlock::~MemoryBlock() noexcept
{
    // 一次性把整块内存归还给 OS，彻底杜绝内存碎片与泄漏
    ::operator delete(rawMemory_);
}

// #include <iostream>
// #include <string>

// struct Player {
//     int id;
//     std::string name;

//     Player(int id, std::string name) : id(id), name(std::move(name)) {
//         std::cout << "Player " << this->name << " created\n";
//     }

//     ~Player() {
//         std::cout << "Player " << this->name << " destroyed\n";
//     }
// };

// int main() {
//     // 实例化一个专属于 Player 的内存池，Block 尺寸采用默认 4096 字节
//     MemoryPool<Player> playerPool;

//     // 1. 分配并构造对象
//     Player* p1 = playerPool.newElement(1, "Alice");
//     Player* p2 = playerPool.newElement(2, "Bob");

//     std::cout << "p1: " << p1->name << ", p2: " << p2->name << "\n";

//     // 2. 析构并回收槽位（槽位进入 FreeList 供下次复用）
//     playerPool.deleteElement(p1);

//     // 3. 再次申请时，会立即复用刚归还的 p1 槽位（O(1) 效率）
//     Player* p3 = playerPool.newElement(3, "Charlie");

//     // 4. 清理剩余对象
//     playerPool.deleteElement(p2);
//     playerPool.deleteElement(p3);

//     return 0;
// }
