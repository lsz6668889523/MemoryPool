#include <iostream>
#include <string>
#include "day38.cpp" // 包含你的类定义

// 1. 定义一个自定义类
struct Monster {
    int id;
    std::string name;
    double hp;

    Monster(int i, std::string n, double h) 
        : id(i), name(std::move(n)), hp(h) {
        std::cout << "[构造] 怪物创建: " << name << " (ID: " << id << ")\n";
    }

    ~Monster() {
        std::cout << "[析构] 怪物销毁: " << name << " (ID: " << id << ")\n";
    }

    void attack() const {
        std::cout << name << " 发动攻击，剩余血量: " << hp << "\n";
    }
};

int main(int argc, char const *argv[]) {
    // 实例化管理 Monster 的对象池（每次向底层申请 4KB 大小的内存块）
    ObjectPool<Monster, 4096> monster_pool;

    std::cout << "--- 1. 构造复杂对象 ---\n";
    // 原位构造对象，支持完美转发参数
    Monster* m1 = monster_pool.construct(101, "哥布林", 50.0);
    Monster* m2 = monster_pool.construct(102, "红龙", 5000.0);

    m1->attack();
    m2->attack();

    std::cout << "\n--- 2. 销毁并回收 ---\n";
    // 显式析构对象并归还给自由链表
    monster_pool.destroy(m1);

    std::cout << "\n--- 3. 内存复用测试 ---\n";
    // 此时从空闲链表中取出刚释放的 m1 slot，构造新对象
    Monster* m3 = monster_pool.construct(103, "骷髅兵", 30.0);
    std::cout << "m1 地址: " << m1 << "\n";
    std::cout << "m3 地址: " << m3 << " (若底层是 LIFO 嵌入式指针，通常地址完全一致)\n";

    monster_pool.destroy(m2);
    monster_pool.destroy(m3);

    std::cout << "\n--- 4. 平凡类型测试 (自动跳过析构函数) ---\n";
    ObjectPool<int> int_pool;
    int* num = int_pool.construct(42);
    std::cout << "分配整型值: " << *num << "\n";
    int_pool.destroy(num); // 命中 destroyInternal 的 std::true_type 分支，零开销

    return 0;
}
