#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include <cassert>
#include "MemoryPool.hpp"

struct TestNode
{
    uint64_t id;
    char data[48];
    TestNode(uint64_t i):id(i){}
};

//场景1 ：多block连续扩容与整池释放（测试批量扩容与生命周期兜底）
void testMassiveAllocation()
{
    std::cout<<"[Test 1] 运行多 Block 连续扩容与池析构兜底测试...\n";
    {
        //BlockSize 设为1024字节，每个Slot占56字节，迫使内存池频繁向OS 申请新的Block.
        MemoryPool<TestNode,1024> pool;
        std::vector<TestNode*>nodes;

        //申请1000个对象，跨越数十个Block
        for(uint64_t i=0;i<1000;++i)
        {
            TestNode* node =pool.newElement(i);
            node->data[0] = 'A';
            nodes.push_back(node);
        }

        //仅释放一半，另一半留在池中，随池析构销毁
        for (size_t i = 0; i < 500; i++)
        {
            pool.deleteElement(nodes[i]);
        }
        
    }// pool 在此析构，验证其 BlockHeader 链表是否彻底把全部申请的内存归还 OS（无内存泄漏）
    std::cout << "[Test 1] 通过！\n";
}

// 场景 2：乱序交叉释放测试（破坏性测试 FreeList 是否成环）
void testShuffledFree()
{
    std::cout<<"[Test 2] 运行乱序释放与高频复用测试...\n";
    MemoryPool<TestNode,2048>pool;
    constexpr int TOTOL = 500;
    std::vector<TestNode*>nodes;

    for(uint64_t i = 0;i<TOTOL;i++)
    {
        nodes.push_back(pool.newElement(i));
    }

    //打乱释放顺序
    std::vector<TestNode*>shuffled = nodes;
    std::mt19937 g(1337);
    std::shuffle(shuffled.begin(),shuffled.end(),g);

    //随机释放
    for(auto* ptr:shuffled)
        pool.deleteElement(ptr);
    
    //再次全量重新申请，验证FreeList是否畅通无环，无死锁
    std::vector<TestNode*>newNodes;
    for(uint64_t i =0;i<TOTOL;i++)
        newNodes.push_back(pool.newElement(i+10000));
    for(auto* ptr:newNodes)
        pool.deleteElement(ptr);

    std::cout << "[Test 2] 通过！\n";
}

// 场景 3：极限空指针与单元素边界
void testEdgeCases()
{
    std::cout<<"[Test 3] 运行极限界限测试...\n";
    MemoryPool<TestNode,4096> pool;
    //释放空指针不应崩溃
    pool.deallocate(nullptr);
    pool.deleteElement(nullptr);

    //申请一个并立即释放
    TestNode* p = pool.newElement(999);
    pool.deleteElement(p);
    std::cout << "[Test 3] 通过！\n";
}

int main(int argc, char const *argv[])
{
    std::cout<<"=====开始进行高强度边界压测 =========\n";
    testMassiveAllocation();
    testShuffledFree();
    testEdgeCases();
    std::cout<<"======== 全部边界压测执行完毕 ========\n";

    return 0;
}
