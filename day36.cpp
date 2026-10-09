#pragma once
#include<cstddef>
#include<utility>
#include<new>
#include<algorithm>
#include<cassert>
#include<iostream>
template <typename T,size_t BlockSize = 4096>
class MemoryPool
{
public:
    //确保槽位同时满足对齐、指针和对象尺寸要求
    union Slot
    {
        Slot* next;
        alignas(alignof(T)) char storage[sizeof(T)];
    };
    static_assert(sizeof(Slot) >=sizeof(void*),
        "Slot size must be at least pointer size. Slot必须要大于一个pointersize");

    MemoryPool() noexcept
        :currentBlock_(nullptr),currentSlot_(nullptr),lastSlot_(nullptr),freeListHead_(nullptr){}
    ~MemoryPool() noexcept
    {
        //统一释放向os申请的所有block，杜绝内存泄漏
        BlockHeader* curr = currentBlock_;
        while (curr)
        {
            BlockHeader* next = curr->next;
            //按照原始申请的字节反分配
            ::operator delete(reinterpret_cast<void*>(curr));
            curr = next;
        }
    }
    //禁止拷贝与移动
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    //1. 底层原始字节分配
    Slot* allocateSlot()
    {
        //通道1. 有限复用FreeList 中归还的节点
        if(freeListHead_!=nullptr)
        {
            Slot* result = freeListHead_;
            freeListHead_ = freeListHead_->next;
            return result;
        }

        //通道2：从当前Block顺序切分
        if (currentSlot_>=lastSlot_)
        {
            allocateNewBlock();
        }
        return currentSlot_++;
        
    }

    //底层原始字节回收
    void deallocateSlot(Slot* slot)noexcept
    {
        if(slot)
        {
            //头插法归还至FreeList
            slot->next = freeListHead_;
            freeListHead_ = slot;
        }
    }

    //3. 上层对象级分配：构造对象(Placement New)
    template <typename... Args>
    T* newElement(Args&&...args)
    {
        Slot* slot = allocateSlot();
        //原地构造对象
        return new(slot)T(std::forward<Args>(args)...);
    }

    //4. 上层对象级释放：析构对象并归还内存
    void deleteElement(T*p)noexcept
    {
        if(p)
        {
            p->~T();
            deallocateSlot(reinterpret_cast<Slot*>(p));
        }
    }


private:
    struct BlockHeader
    {
        BlockHeader* next;
    };
    //申请新的 Block 并挂载
    void allocateNewBlock()
    {
        //动态分配大内存块
        void* rawMemory = ::operator new(BlockSize);
        BlockHeader* newBlock = reinterpret_cast<BlockHeader*>(rawMemory);
        newBlock->next = currentBlock_;
        currentBlock_ = newBlock;

        //计算可供切分为Slot 的有效内存起始与结束地址
        char* body = reinterpret_cast<char*>(rawMemory)+sizeof(BlockHeader);

        //地址对齐到alignof(Slot)
        size_t bodyAddress = reinterpret_cast<size_t>(body);
        size_t alignment = alignof(Slot);
        size_t alignedAddress = (bodyAddress+alignment-1) & ~(alignment-1);
        currentSlot_ = reinterpret_cast<Slot*>(alignedAddress);

        //计算当前Block最多能容纳多少个完整的Slot
        size_t maxSlots = (reinterpret_cast<char*>(rawMemory)+BlockSize - reinterpret_cast<char*>(currentSlot_))/sizeof(Slot);
        
        lastSlot_ = currentSlot_ + maxSlots;
    }
    
private:
    BlockHeader* currentBlock_;//记录所有申请的Block链表头
    Slot* currentSlot_;//当前Block 未切分区域的游标起点
    Slot* lastSlot_;//当前Block可用区域的上限
    Slot* freeListHead_;//空闲已回收的链表头

};


struct Point
{
    int x,y;
    Point(int a,int b):x(a),y(b){}
};

int main(int argc, char const *argv[])
{
    MemoryPool<Point> pool;
    Point* p1  = pool.newElement(10,20);
    std::cout << "分配 p1 地址: " << p1 << " (值: " << p1->x << ", " << p1->y << ")\n";

    pool.deleteElement(p1);
    Point* p2 = pool.newElement(30,40);
    std::cout << "分配 p2 地址: " << p2 << " (值: " << p2->x << ", " << p2->y << ")\n";

    assert(p1 == p2);
    std::cout << ">>> 验证成功：p2 精确复用了 p1 的物理槽位，FreeList 逻辑正确！<<<\n";
    pool.deleteElement(p2);
    return 0;
}

