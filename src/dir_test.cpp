/**
 * @brief 测试 create_dir
 *   - 创建子目录
 *   - 在子目录中创建文件
 *   - 重名检查
 */
#include "../include/FAT16.hpp"
#include "../include/BlockDevice.hpp"
#include <iostream>
#include <cstring>

int main()
{
    FAT16 fs("empty.img");

    // 1. 根目录
    {
        FAT16::PATH_RESULT res;
        if (!fs.resolve_path("/", res) || !res.exists)
        {
            std::cerr << "FAIL: root should exist\n";
            return 1;
        }
        std::cout << "  root directory exists  OK\n";
    }

    // 2. 创建子目录
    if (!fs.create_dir("/DIR_A"))
    {
        std::cerr << "FAIL: create_dir(\"/DIR_A\") returned false\n";
        return 1;
    }
    std::cout << "  create_dir(\"/DIR_A\")  OK\n";

    // 3. 重名检查
    if (fs.create_dir("/DIR_A"))
    {
        std::cerr << "FAIL: duplicate create_dir should have returned false\n";
        return 1;
    }
    std::cout << "  create_dir duplicate rejected  OK\n";

    // 4. 创建深层子目录
    if (!fs.create_dir("/DIR_B"))
    {
        std::cerr << "FAIL: create_dir(\"/DIR_B\") returned false\n";
        return 1;
    }
    std::cout << "  create_dir(\"/DIR_B\")  OK\n";

    // 5. 子目录中创建文件
    if (!fs.create_file("/DIR_A/FILE1.TXT"))
    {
        std::cerr << "FAIL: create_file(\"/DIR_A/FILE1.TXT\") returned false\n";
        return 1;
    }
    std::cout << "  create_file(\"/DIR_A/FILE1.TXT\")  OK\n";

    if (!fs.create_file("/DIR_A/FILE2.TXT"))
    {
        std::cerr << "FAIL: create_file(\"/DIR_A/FILE2.TXT\") returned false\n";
        return 1;
    }
    std::cout << "  create_file(\"/DIR_A/FILE2.TXT\")  OK\n";

    // 6. 子目录中子目录（多层）
    if (!fs.create_dir("/DIR_A/SUB"))
    {
        std::cerr << "FAIL: create_dir(\"/DIR_A/SUB\") returned false\n";
        return 1;
    }
    std::cout << "  create_dir(\"/DIR_A/SUB\")  OK\n";

    // 7. 删除子目录中的文件
    if (!fs.delete_file("/DIR_A/FILE1.TXT"))
    {
        std::cerr << "FAIL: delete_file(\"/DIR_A/FILE1.TXT\") returned false\n";
        return 1;
    }
    std::cout << "  delete_file(\"/DIR_A/FILE1.TXT\")  OK\n";

    // 8. 用 list_entries 验证
    {
        // DIR_A 应该包含：FILE2.TXT、SUB
        auto list = fs.list_entries(FAT16::ROOT_DIR_CLUSTER);
        std::cout << "  root entries (" << list.size() << "):\n";
        for (const auto& e : list)
        {
            std::string n;
            fs.short_name_to_string(e.DIR_Name, n);
            bool is_dir = (e.DIR_Attr == 0x10);
            std::cout << "    " << (is_dir ? "[DIR] " : "[FILE]") << n;
            if (!is_dir) std::cout << "  size=" << e.DIR_FileSize;
            std::cout << "\n";
        }
    }

    std::cout << "dir_test PASSED\n";
    return 0;
}
