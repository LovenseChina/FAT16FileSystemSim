/**
 * @brief 测试路径解析相关的各函数：
 *   - assist_normalize
 *   - validation_name
 *   - path_normalizer
 *   - path_simplify
 *   - resolve_path
 */
#include "../include/FAT16.hpp"
#include "../include/BlockDevice.hpp"
#include <iostream>
#include <cstring>
#include <string>

int main()
{
    FAT16 fs("empty.img");

    // ========== 1. assist_normalize ==========
    {
        std::string result = fs.assist_normalize("//dir1///dir2/../file.txt");
        // 应当去除重复 '/'，转大写
        if (result != "/DIR1/DIR2/../FILE.TXT")
        {
            std::cerr << "FAIL: assist_normalize = \"" << result << "\", expected \"/DIR1/DIR2/../FILE.TXT\"\n";
            return 1;
        }
        std::cout << "  assist_normalize(\"//dir1///dir2/../file.txt\") = \"" << result << "\"  OK\n";
    }

    // ========== 2. validation_name ==========
    {
        // 合法
        if (!fs.validation_name("FILE.TXT"))
        {
            std::cerr << "FAIL: validation_name(\"FILE.TXT\") must be true\n";
            return 1;
        }
        // 合法（无扩展名）
        if (!fs.validation_name("FILENAME"))
        {
            std::cerr << "FAIL: validation_name(\"FILENAME\") must be true\n";
            return 1;
        }
        // 非法（名字部分超过8）
        if (fs.validation_name("123456789.TXT"))
        {
            std::cerr << "FAIL: validation_name(\"123456789.TXT\") must be false\n";
            return 1;
        }
        // 非法（扩展名超过3）
        if (fs.validation_name("FILE.TXTA"))
        {
            std::cerr << "FAIL: validation_name(\"FILE.TXTA\") must be false\n";
            return 1;
        }
        // 非法（超过12字符）
        if (fs.validation_name("12345678.1234"))
        {
            std::cerr << "FAIL: validation_name(\"12345678.1234\") must be false\n";
            return 1;
        }
        // 合法（特殊字符 . 和 ..）
        if (!fs.validation_name("."))
        {
            std::cerr << "FAIL: validation_name(\".\") must be true\n";
            return 1;
        }
        if (!fs.validation_name(".."))
        {
            std::cerr << "FAIL: validation_name(\"..\") must be true\n";
            return 1;
        }
        std::cout << "  validation_name() multiple cases  OK\n";
    }

    // ========== 3. path_normalizer ==========
    {
        std::string out;
        if (!fs.path_normalizer("//Dir1//FILE.TXT", out))
        {
            std::cerr << "FAIL: path_normalizer(\"//Dir1//FILE.TXT\") returned false\n";
            return 1;
        }
        if (out != "/DIR1/FILE.TXT")
        {
            std::cerr << "FAIL: path_normalizer = \"" << out << "\", expected \"/DIR1/FILE.TXT\"\n";
            return 1;
        }
        std::cout << "  path_normalizer(\"//Dir1//FILE.TXT\") = \"" << out << "\"  OK\n";

        // 非法路径
        if (fs.path_normalizer("/INVALID<FILE>.TXT", out))
        {
            std::cerr << "FAIL: path_normalizer(\"/INVALID<FILE>.TXT\") should be false\n";
            return 1;
        }
        std::cout << "  path_normalizer() rejects invalid chars  OK\n";
    }

    // ========== 4. path_simplify ==========
    {
        std::string out;
        // 根目录
        if (!fs.path_simplify("/", out) || out != "/")
        {
            std::cerr << "FAIL: path_simplify(\"/\") = \"" << out << "\"\n";
            return 1;
        }
        // 空
        if (!fs.path_simplify("/.", out) || out != "/")
        {
            std::cerr << "FAIL: path_simplify(\"/.\") = \"" << out << "\"\n";
            return 1;
        }
        // .. 到根
        if (!fs.path_simplify("/..", out) || out != "/")
        {
            std::cerr << "FAIL: path_simplify(\"/..\") = \"" << out << "\"\n";
            return 1;
        }
        // 简单
        if (!fs.path_simplify("/DIR1/DIR2/../DIR3", out) || out != "/DIR1/DIR3")
        {
            std::cerr << "FAIL: path_simplify(\"/DIR1/DIR2/../DIR3\") = \"" << out << "\"\n";
            return 1;
        }
        std::cout << "  path_simplify() multiple cases  OK\n";
    }

    // ========== 5. resolve_path ==========
    {
        FAT16::PATH_RESULT res;
        // 根目录
        if (!fs.resolve_path("/", res))
        {
            std::cerr << "FAIL: resolve_path(\"/\") returned false\n";
            return 1;
        }
        if (!res.exists)
        {
            std::cerr << "FAIL: resolve_path(\"/\") exists = false\n";
            return 1;
        }
        if (res.parent_filename != "")
        {
            std::cerr << "FAIL: resolve_path(\"/\") parent_filename = \"" << res.parent_filename << "\"\n";
            return 1;
        }
        std::cout << "  resolve_path(\"/\")  OK\n";

        // 不存在的文件
        if (!fs.resolve_path("/NOTEXIST.TXT", res))
        {
            std::cerr << "FAIL: resolve_path(\"/NOTEXIST.TXT\") returned false\n";
            return 1;
        }
        if (res.exists)
        {
            std::cerr << "FAIL: resolve_path(\"/NOTEXIST.TXT\") exists should be false\n";
            return 1;
        }
        if (res.parent_dir_cluster != FAT16::ROOT_DIR_CLUSTER)
        {
            std::cerr << "FAIL: parent_dir_cluster should be ROOT\n";
            return 1;
        }
        std::cout << "  resolve_path(\"/NOTEXIST.TXT\") not found, parent is root  OK\n";
    }

    std::cout << "path_test PASSED\n";
    return 0;
}
