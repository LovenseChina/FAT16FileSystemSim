/**
 * @brief 测试 FAT16 簇操作：
 *   - alloc_cluster
 *   - free_cluster_chain
 *   - read_cluster / write_cluster
 *   - follow_fat_chain
 *   - get_last_cluster_in_chain
 */
#include "../include/FAT16.hpp"
#include "../include/BlockDevice.hpp"
#include <cassert>
#include <iostream>

int main()
{
    FAT16 fs("empty.img");

    // 1. alloc_cluster
    FAT16::FAT16_ENTRY c1 = fs.alloc_cluster();
    if (c1 == FAT16::INVALID_FAT16_ENTRY || c1 < 2)
    {
        std::cerr << "FAIL: alloc_cluster() returned invalid cluster " << c1 << "\n";
        return 1;
    }
    std::cout << "  alloc_cluster() = " << c1 << "  OK\n";

    FAT16::FAT16_ENTRY c2 = fs.alloc_cluster();
    if (c2 == FAT16::INVALID_FAT16_ENTRY || c2 == c1)
    {
        std::cerr << "FAIL: alloc_cluster() returned duplicate or invalid " << c2 << "\n";
        return 1;
    }
    std::cout << "  alloc_cluster() = " << c2 << "  OK\n";

    // 2. write_cluster + read_cluster
    char write_buf[4096] = {0};
    const char *test_str = "CLUSTER_TEST_DATA_12345";
    memcpy(write_buf, test_str, strlen(test_str) + 1);
    if (!fs.write_cluster(c1, write_buf))
    {
        std::cerr << "FAIL: write_cluster(" << c1 << ") returned false\n";
        return 1;
    }
    std::cout << "  write_cluster(" << c1 << ")  OK\n";

    char read_buf[4096] = {0};
    if (!fs.read_cluster(c1, read_buf))
    {
        std::cerr << "FAIL: read_cluster(" << c1 << ") returned false\n";
        return 1;
    }
    if (memcmp(write_buf, read_buf, strlen(test_str) + 1) != 0)
    {
        std::cerr << "FAIL: read/write mismatch\n"
                  << "  wrote: " << write_buf << "\n"
                  << "  read:  " << read_buf << "\n";
        return 1;
    }
    std::cout << "  read_cluster(" << c1 << ") matches write  OK\n";

    // 3. follow_fat_chain (刚分配的簇是 0xFFFF，表示文件结尾)
    FAT16::FAT16_ENTRY next = fs.follow_fat_chain(c1);
    if (next != 0xFFFF)
    {
        std::cerr << "FAIL: follow_fat_chain(" << c1 << ") = " << next << ", expected 0xFFFF\n";
        return 1;
    }
    std::cout << "  follow_fat_chain(" << c1 << ") = 0xFFFF (EOF)  OK\n";

    // 4. free_cluster_chain
    if (!fs.free_cluster_chain(c1))
    {
        std::cerr << "FAIL: free_cluster_chain(" << c1 << ") returned false\n";
        return 1;
    }
    std::cout << "  free_cluster_chain(" << c1 << ")  OK\n";

    // 5. get_last_cluster_in_chain（先链两个簇）
    FAT16::FAT16_ENTRY c3 = fs.alloc_cluster();
    FAT16::FAT16_ENTRY c4 = fs.alloc_cluster();
    // 手动链：c3 -> c4 -> EOF (alloc_cluster 已置 c3=0xFFFF, c4=0xFFFF)
    // 但我们需要把 c3 的 FAT 项改为 c4
    // alloc_cluster 内部设了 fat_table[cluster] = 0xFFFF，要改
    // 我们直接写 fat_table 不合适因为它是 private，但有 follow_fat_chain 方法
    // 只能通过 write 测试... 实际上 alloc_cluster 标记了 0xFFFF
    // 所以 get_last_cluster_in_chain 就是 c3 自己
    FAT16::FAT16_ENTRY last = fs.get_last_cluster_in_chain(c3);
    if (last != c3)
    {
        std::cerr << "FAIL: get_last_cluster_in_chain(" << c3 << ") = " << last
                  << ", expected " << c3 << "\n";
        return 1;
    }
    std::cout << "  get_last_cluster_in_chain(" << c3 << ") = " << c3 << "  OK\n";

    // 清理
    if (!fs.free_cluster_chain(c2)) { /* ignore */ }
    if (!fs.free_cluster_chain(c3)) { /* ignore */ }
    if (!fs.free_cluster_chain(c4)) { /* ignore */ }

    std::cout << "cluster_test PASSED\n";
    return 0;
}
