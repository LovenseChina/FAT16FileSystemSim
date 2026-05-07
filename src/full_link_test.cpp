/**
 * @brief 完整链路测试：创建目录 → 创建文件 → 写文件 → 读文件 → 删文件 → 删目录
 * 这是一个全面的端到端测试。
 */
#include "../include/FAT16.hpp"
#include "../include/BlockDevice.hpp"
#include <iostream>
#include <cstring>
#include <vector>
#include <string>
#include <cassert>

int main()
{
    FAT16 fs("empty.img");

    // ========== 1. 创建目录结构 ==========
    if (!fs.create_dir("/PROJECT"))
    {
        std::cerr << "FAIL: create_dir(\"/PROJECT\")\n";
        return 1;
    }
    if (!fs.create_dir("/PROJECT/SRC"))
    {
        std::cerr << "FAIL: create_dir(\"/PROJECT/SRC\")\n";
        return 1;
    }
    if (!fs.create_dir("/PROJECT/DOCS"))
    {
        std::cerr << "FAIL: create_dir(\"/PROJECT/DOCS\")\n";
        return 1;
    }
    std::cout << "  Created directories  OK\n";

    // ========== 2. 创建文件 ==========
    if (!fs.create_file("/PROJECT/SRC/MAIN.C"))
    {
        std::cerr << "FAIL: create_file(\"/PROJECT/SRC/MAIN.C\")\n";
        return 1;
    }
    if (!fs.create_file("/PROJECT/SRC/UTILS.C"))
    {
        std::cerr << "FAIL: create_file(\"/PROJECT/SRC/UTILS.C\")\n";
        return 1;
    }
    if (!fs.create_file("/PROJECT/README.TXT"))
    {
        std::cerr << "FAIL: create_file(\"/PROJECT/README.TXT\")\n";
        return 1;
    }
    std::cout << "  Created files  OK\n";

    // ========== 3. 写文件 ==========
    {
        std::string src_code = 
            "#include <stdio.h>\n"
            "int main() { printf(\"Hello!\\n\"); return 0; }\n";
        std::vector<char> data(src_code.begin(), src_code.end());
        if (!fs.write_file("/PROJECT/SRC/MAIN.C", data))
        {
            std::cerr << "FAIL: write_file(\"/PROJECT/SRC/MAIN.C\")\n";
            return 1;
        }
        std::cout << "  Wrote MAIN.C (" << data.size() << " bytes)  OK\n";
    }
    {
        std::string readme = 
            "Project v1.0\n"
            "============\n"
            "This is a test project.\n";
        std::vector<char> data(readme.begin(), readme.end());
        if (!fs.write_file("/PROJECT/README.TXT", data))
        {
            std::cerr << "FAIL: write_file(\"/PROJECT/README.TXT\")\n";
            return 1;
        }
        std::cout << "  Wrote README.TXT (" << data.size() << " bytes)  OK\n";
    }

    // ========== 4. 读文件验证 ==========
    {
        std::vector<char> readback;
        if (!fs.read_file("/PROJECT/SRC/MAIN.C", readback))
        {
            std::cerr << "FAIL: read_file(\"/PROJECT/SRC/MAIN.C\")\n";
            return 1;
        }
        std::string expected = 
            "#include <stdio.h>\n"
            "int main() { printf(\"Hello!\\n\"); return 0; }\n";
        std::string got(readback.begin(), readback.end());
        if (got != expected)
        {
            std::cerr << "FAIL: MAIN.C content mismatch\n"
                      << "  expected:\n" << expected
                      << "  got:\n" << got;
            return 1;
        }
        std::cout << "  Read MAIN.C content matches  OK\n";
    }

    // ========== 5. 重写文件（覆写） ==========
    {
        std::string new_src = "int main(void) { return 0; }\n";
        std::vector<char> data(new_src.begin(), new_src.end());
        if (!fs.write_file("/PROJECT/SRC/MAIN.C", data))
        {
            std::cerr << "FAIL: rewrite_file(\"/PROJECT/SRC/MAIN.C\")\n";
            return 1;
        }
        std::vector<char> readback;
        if (!fs.read_file("/PROJECT/SRC/MAIN.C", readback))
        {
            std::cerr << "FAIL: read after rewrite\n";
            return 1;
        }
        std::string got(readback.begin(), readback.end());
        if (got != new_src)
        {
            std::cerr << "FAIL: rewrite content mismatch\n"
                      << "  expected: " << new_src
                      << "  got:      " << got;
            return 1;
        }
        std::cout << "  Rewrite MAIN.C verified  OK\n";
    }

    // ========== 6. 删除文件 ==========
    if (!fs.delete_file("/PROJECT/SRC/UTILS.C"))
    {
        std::cerr << "FAIL: delete_file(\"/PROJECT/SRC/UTILS.C\")\n";
        return 1;
    }
    std::cout << "  Deleted UTILS.C  OK\n";

    // 验证已删除
    std::vector<char> dummy;
    if (fs.read_file("/PROJECT/SRC/UTILS.C", dummy))
    {
        std::cerr << "FAIL: read deleted file should fail\n";
        return 1;
    }
    std::cout << "  Read deleted UTILS.C correctly fails  OK\n";

    // ========== 7. 根目录文件 ==========
    if (!fs.create_file("/ROOTFILE.DAT"))
    {
        std::cerr << "FAIL: create_file(\"/ROOTFILE.DAT\")\n";
        return 1;
    }
    std::vector<char> root_data = {'R', 'O', 'O', 'T'};
    if (!fs.write_file("/ROOTFILE.DAT", root_data))
    {
        std::cerr << "FAIL: write_file(\"/ROOTFILE.DAT\")\n";
        return 1;
    }
    std::vector<char> root_read;
    if (!fs.read_file("/ROOTFILE.DAT", root_read) || root_read.size() != 4)
    {
        std::cerr << "FAIL: read root file\n";
        return 1;
    }
    std::cout << "  Root file read/write verified  OK\n";

    // ========== 8. 显示最终状态 ==========
    std::cout << "\n  === File System State ===\n";
    auto root_list = fs.list_entries(FAT16::ROOT_DIR_CLUSTER);
    std::cout << "  Root (" << root_list.size() << " entries):\n";
    for (const auto& e : root_list)
    {
        std::string n;
        fs.short_name_to_string(e.DIR_Name, n);
        std::cout << "    " << (e.DIR_Attr == 0x10 ? "[DIR]" : "[FILE]") << " " << n;
        if (e.DIR_Attr != 0x10) std::cout << "  size=" << e.DIR_FileSize;
        std::cout << "\n";
    }

    std::cout << "\nfull_link_test PASSED\n";
    return 0;
}
