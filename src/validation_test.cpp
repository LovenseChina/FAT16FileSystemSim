/**
 * @brief 测试镜像验证：
 *   - validation_fat16（构造函数内部自动调用）
 *   - get_total_clusters
 *   - LBA_to_PA
 */
#include "../include/FAT16.hpp"
#include "../include/BlockDevice.hpp"
#include <iostream>
#include <cassert>

int main()
{
    // 构造函数会自动调用 validation_fat16，能通过就说明验证通过了
    FAT16 fs("empty.img");
    std::cout << "  FAT16 constructor + validation_fat16()  OK\n";

    // get_total_clusters
    uint32_t total = fs.get_total_clusters();
    std::cout << "  get_total_clusters() = " << total << "\n";
    if (total == 0)
    {
        std::cerr << "FAIL: total clusters = 0\n";
        return 1;
    }
    // 预期值计算：
    // BPB_TotSec32 = 65617
    // RsvdSecCnt = 1
    // BPB_FATSz16 = 32, BPB_NumFATs = 2 → FAT 区 = 64
    // RootEntCnt = 512, BytsPerSec = 512 → 根目录 = 32 扇区
    // 数据区 = 65617 - 1 - 64 - 32 = 65520
    // 簇数 = 65520 / 8 = 8190
    if (total != 8190)
    {
        std::cerr << "FAIL: expected 8190 clusters, got " << total << "\n";
        return 1;
    }
    std::cout << "  total clusters = 8190 (expected)  OK\n";

    // LBA_to_PA
    uint32_t block_id;
    if (!fs.LBA_to_PA(2, block_id))
    {
        std::cerr << "FAIL: LBA_to_PA(2) returned false\n";
        return 1;
    }
    // 首数据簇 = RsvdSecCnt + NumFATs * FATSz16 + (RootEntCnt * 32 / BytsPerSec)
    //          = 1 + 2*32 + 512*32/512 = 1 + 64 + 32 = 97
    // 簇2 的起始扇区 = 97 + (2-2)*8 = 97
    if (block_id != 97)
    {
        std::cerr << "FAIL: LBA_to_PA(2) = " << block_id << ", expected 97\n";
        return 1;
    }
    std::cout << "  LBA_to_PA(2) = 97 (expected)  OK\n";

    // 非法簇号
    if (fs.LBA_to_PA(0, block_id))
    {
        std::cerr << "FAIL: LBA_to_PA(0) should have returned false\n";
        return 1;
    }
    if (fs.LBA_to_PA(1, block_id))
    {
        std::cerr << "FAIL: LBA_to_PA(1) should have returned false\n";
        return 1;
    }
    std::cout << "  LBA_to_PA() rejects invalid clusters  OK\n";

    std::cout << "validation_test PASSED\n";
    return 0;
}
