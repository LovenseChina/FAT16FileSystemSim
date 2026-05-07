/**
 * @brief 测试目录项操作：
 *   - add_entry (根目录)
 *   - find_entry (根目录)
 *   - remove_entry
 *   - list_entries
 *   - short_name_to_string
 */
#include "../include/FAT16.hpp"
#include "../include/BlockDevice.hpp"
#include <iostream>
#include <cstring>

int main()
{
    FAT16 fs("empty.img");

    // 准备一个目录项
    FAT16::DIR_ENTRY ent;
    memset(&ent, 0, sizeof(ent));
    memset(ent.DIR_Name, ' ', 11);
    memcpy(ent.DIR_Name, "TEST    TXT", 11); // "TEST.TXT" 8.3 格式
    ent.DIR_Attr = 0x00;
    ent.DIR_FstClusHI = 0x0000;
    ent.DIR_FstClusLO = 0xFFFF;
    ent.DIR_FileSize = 0;

    // 1. add_entry 到根目录
    if (!fs.add_entry(FAT16::ROOT_DIR_CLUSTER, ent))
    {
        std::cerr << "FAIL: add_entry(ROOT, TEST.TXT) returned false\n";
        return 1;
    }
    std::cout << "  add_entry(ROOT, \"TEST.TXT\")  OK\n";

    // 2. find_entry 查找
    FAT16::DIR_ENTRY found;
    if (!fs.find_entry(FAT16::ROOT_DIR_CLUSTER, "TEST.TXT", found))
    {
        std::cerr << "FAIL: find_entry(ROOT, \"TEST.TXT\") returned false\n";
        return 1;
    }
    std::cout << "  find_entry(ROOT, \"TEST.TXT\") found  OK\n";

    // 3. short_name_to_string 验证
    std::string name;
    fs.short_name_to_string(found.DIR_Name, name);
    if (name != "TEST.TXT")
    {
        std::cerr << "FAIL: short_name_to_string = \"" << name << "\", expected \"TEST.TXT\"\n";
        return 1;
    }
    std::cout << "  short_name_to_string = \"" << name << "\"  OK\n";

    // 4. list_entries 应包含刚才添加的项
    std::vector<FAT16::DIR_ENTRY> list = fs.list_entries(FAT16::ROOT_DIR_CLUSTER);
    bool found_in_list = false;
    for (const auto &e : list)
    {
        std::string n;
        fs.short_name_to_string(e.DIR_Name, n);
        if (n == "TEST.TXT")
            found_in_list = true;
    }
    if (!found_in_list)
    {
        std::cerr << "FAIL: list_entries(ROOT) did not contain \"TEST.TXT\"\n";
        return 1;
    }
    std::cout << "  list_entries(ROOT) contains \"TEST.TXT\"  OK\n";

    // 5. remove_entry
    if (!fs.remove_entry(FAT16::ROOT_DIR_CLUSTER, "TEST.TXT"))
    {
        std::cerr << "FAIL: remove_entry(ROOT, \"TEST.TXT\") returned false\n";
        return 1;
    }
    std::cout << "  remove_entry(ROOT, \"TEST.TXT\")  OK\n";

    // 6. 验证已删除
    if (fs.find_entry(FAT16::ROOT_DIR_CLUSTER, "TEST.TXT", found))
    {
        std::cerr << "FAIL: find_entry still found after remove_entry\n";
        return 1;
    }
    std::cout << "  find_entry after remove correctly returns false  OK\n";

    std::cout << "entry_test PASSED\n";
    return 0;
}
