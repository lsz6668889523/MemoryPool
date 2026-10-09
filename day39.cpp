#include "day37.cpp"
#include <vector>
#include <benchmark/benchmark.h>
//模拟典型的高频业务小对象
struct SmallPacket
{
    int id;
    int type;
    char payload[16];
};

constexpr int BATCH_SIZE = 10000;

//1 系统自带malloc/free测试
static void BM_SystemMalloc(benchmark::State& state)
{
    std::vector<SmallPacket*> ptrs(BATCH_SIZE);

    for(auto _:state)
    {
        //批量分配
        for(int i=0;i<BATCH_SIZE;i++)
        {
            ptrs[i] = static_cast<SmallPacket*>(::malloc(sizeof(SmallPacket)));
            benchmark::DoNotOptimize(ptrs[i]);
        }
        //批量释放
        for (int i = 0; i < BATCH_SIZE; i++)
        {
            ::free(ptrs[i]);
        }
    }
    //统计总处理对象是数，自动输出QPS（items/s）
    state.SetItemsProcessed(state.iterations()*BATCH_SIZE);
}
BENCHMARK(BM_SystemMalloc);

//手写定长MemoryPool基准测试
static void BM_CustomMemoryPool(benchmark::State& state)
{
    MemoryPool<SmallPacket,65536>pool;
    std::vector<SmallPacket*> ptrs(BATCH_SIZE);

    for(auto _ :state)
    {
        for(int i=0;i<BATCH_SIZE;i++)
        {
            ptrs[i] = static_cast<SmallPacket*>(pool.allocate());
            benchmark::DoNotOptimize(ptrs[i]);
        }
        for (int i = 0; i < BATCH_SIZE; i++)
        {
            pool.deallocate(ptrs[i]);

        }
    }
    // 统计总处理对象数，自动输出 QPS (items/s)
    state.SetItemsProcessed(state.iterations() * BATCH_SIZE);
}
BENCHMARK(BM_CustomMemoryPool);

BENCHMARK_MAIN();