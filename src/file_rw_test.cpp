/**
 * @brief 测试 read_file / write_file
 */
#include "../include/FAT16.hpp"
#include "../include/BlockDevice.hpp"
#include <iostream>
#include <cstring>
#include <vector>
#include <string>

int main()
{
    FAT16 fs("empty.img");

    // 1. 创建文件
    if (!fs.create_file("/DATA.BIN"))
    {
        std::cerr << "FAIL: create_file(\"/DATA.BIN\")\n";
        return 1;
    }
    std::cout << "  create_file(\"/DATA.BIN\")  OK\n";

    // 2. write_file
    const char *content = "Hello FAT16 filesystem! This is a test.";
    std::vector<char> data(content, content + strlen(content) + 1); // 含 '\0'
    if (!fs.write_file("/DATA.BIN", data))
    {
        std::cerr << "FAIL: write_file(\"/DATA.BIN\") returned false\n";
        return 1;
    }
    std::cout << "  write_file(\"/DATA.BIN\") " << data.size() << " bytes  OK\n";

    // 3. read_file
    std::vector<char> readback;
    if (!fs.read_file("/DATA.BIN", readback))
    {
        std::cerr << "FAIL: read_file(\"/DATA.BIN\") returned false\n";
        return 1;
    }
    if (readback.size() != data.size())
    {
        std::cerr << "FAIL: read size = " << readback.size()
                  << ", expected " << data.size() << "\n";
        return 1;
    }
    if (memcmp(readback.data(), data.data(), data.size()) != 0)
    {
        std::cerr << "FAIL: read data mismatch\n"
                  << "  expected: \"" << data.data() << "\"\n"
                  << "  got:      \"" << readback.data() << "\"\n";
        return 1;
    }
    std::cout << "  read_file(\"/DATA.BIN\") matches write  OK\n";

    // 4. 覆写更长的数据
    std::vector<char> big_data;
    for (int i = 0; i < 5000; ++i)
        big_data.push_back(static_cast<char>(i % 256));
    if (!fs.write_file("/DATA.BIN", big_data))
    {
        std::cerr << "FAIL: write_file big data failed\n";
        return 1;
    }
    std::cout << "  write_file 5000 bytes  OK\n";

    readback.clear();
    if (!fs.read_file("/DATA.BIN", readback))
    {
        std::cerr << "FAIL: read_file big data failed\n";
        return 1;
    }
    if (readback.size() != big_data.size())
    {
        std::cerr << "FAIL: read big data size = " << readback.size()
                  << ", expected " << big_data.size() << "\n";
        return 1;
    }
    if (memcmp(readback.data(), big_data.data(), big_data.size()) != 0)
    {
        std::cerr << "FAIL: big data mismatch at byte ";
        for (size_t i = 0; i < big_data.size(); ++i)
        {
            if (readback[i] != big_data[i])
            {
                std::cerr << i << "\n";
                return 1;
            }
        }
    }
    std::cout << "  read_file 5000 bytes matches  OK\n";

    // 5. 删除后读取应失败
    if (!fs.delete_file("/DATA.BIN"))
    {
        std::cerr << "FAIL: delete_file(\"/DATA.BIN\")\n";
        return 1;
    }
    std::cout << "  delete_file(\"/DATA.BIN\")  OK\n";

    if (fs.read_file("/DATA.BIN", readback))
    {
        std::cerr << "FAIL: read deleted file should have failed\n";
        return 1;
    }
    std::cout << "  read deleted file rejected  OK\n";

    std::cout << "file_rw_test PASSED\n";
    return 0;
}
