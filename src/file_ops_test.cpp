/**
 * @brief 测试 create_file 和 delete_file
 */
#include "../include/FAT16.hpp"
#include "../include/BlockDevice.hpp"
#include <iostream>
#include <cstring>

int main()
{
    FAT16 fs("empty.img");

    // ========== 1. create_file 根目录 ==========
    if (!fs.create_file("/TEST.TXT"))
    {
        std::cerr << "FAIL: create_file(\"/TEST.TXT\") returned false\n";
        return 1;
    }
    std::cout << "  create_file(\"/TEST.TXT\")  OK\n";

    // 重复创建应被拒绝
    if (fs.create_file("/TEST.TXT"))
    {
        std::cerr << "FAIL: duplicate create_file should have returned false\n";
        return 1;
    }
    std::cout << "  create_file duplicate rejected  OK\n";

    // ========== 2. create_file 子目录路径（但子目录还不存在） ==========
    // 如果子目录不存在，resolve_path 会返回 false（中间目录不存在）
    if (fs.create_file("/SUBDIR/MYFILE.TXT"))
    {
        std::cerr << "FAIL: create_file(\"/SUBDIR/MYFILE.TXT\") should have failed (SUBDIR doesn't exist)\n";
        return 1;
    }
    std::cout << "  create_file in non-existent subdir rejected  OK\n";

    // ========== 3. 删除文件 ==========
    if (!fs.delete_file("/TEST.TXT"))
    {
        std::cerr << "FAIL: delete_file(\"/TEST.TXT\") returned false\n";
        return 1;
    }
    std::cout << "  delete_file(\"/TEST.TXT\")  OK\n";

    // 重复删除应失败
    if (fs.delete_file("/TEST.TXT"))
    {
        std::cerr << "FAIL: delete already-deleted file should have returned false\n";
        return 1;
    }
    std::cout << "  delete_file duplicate rejected  OK\n";

    // ========== 4. 创建子目录中的文件（先创建子目录） ==========
    if (!fs.create_dir("/MYDIR"))
    {
        std::cerr << "FAIL: create_dir(\"/MYDIR\") returned false\n";
        return 1;
    }
    std::cout << "  create_dir(\"/MYDIR\")  OK\n";

    if (!fs.create_file("/MYDIR/A.TXT"))
    {
        std::cerr << "FAIL: create_file(\"/MYDIR/A.TXT\") returned false\n";
        return 1;
    }
    std::cout << "  create_file(\"/MYDIR/A.TXT\")  OK\n";

    if (!fs.delete_file("/MYDIR/A.TXT"))
    {
        std::cerr << "FAIL: delete_file(\"/MYDIR/A.TXT\") returned false\n";
        return 1;
    }
    std::cout << "  delete_file(\"/MYDIR/A.TXT\")  OK\n";

    std::cout << "file_ops_test PASSED\n";
    return 0;
}
