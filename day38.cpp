#pragma once
#include <utility>
#include <new>
#include <type_traits>
#include <cassert>
#include "day37.cpp"

template <typename T,size_t BlockSize = 4096>
class ObjectPool
{
private:
    MemoryPool<T,BlockSize>pool_;// 底层组合固定尺寸原始内存池
    // 对于非平凡析构类型，显式调用析构函数
    void destroyInternal(T* ptr,std::false_type) noexcept {ptr->~T();}
    // 对于平凡析构类型 (如 int, float, POD结构体)，无需调用析构函数
    void destroyInternal(T*,std::true_type) noexcept {}

public:
    using valuetype = T;
    ObjectPool() = default;
    ~ObjectPool() = default;
    //禁止拷贝与移动
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    // 1. 原位构造对象 (对应 boost::object_pool::construct)
    template<typename... Args>
    T* construct(Args&&... args)
    {
        // 从底层申请未初始化的裸内存 (至少确保 sizeof(T) 和对齐)
        void* mem = pool_.allocate();
        try
        {
            // 在预先分配的裸内存上直接调用构造函数
            return new(mem) T(std::forward<Args>(args)...);
        }
        catch(...)
        {
            // 构造函数抛出异常时的安全保障：归还内存，防止内存泄漏
            pool_.deallocate(mem);
            throw;
        }
    }
    // 2. 显式析构并归还内存 (对应 boost::object_pool::destroy)
    void destroy(T* ptr)noexcept
    {
        if(!ptr)return ;
        // 显式析构对象
        // 利用 SFINAE/编译期优化：如果是平凡析构类型(Trivially Destructible)，析构可被优化为空操作
        destroyInternal(ptr,std::is_trivially_destructible<T>{});
        // 将裸内存交回底层 FreeList
        pool_.deallocate(ptr);
    }

};

