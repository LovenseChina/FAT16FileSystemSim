#include <iostream>
#include <fstream>
#include <cstdint>
#include <cstdlib>

int main() {
    const std::string filename = "endian_checker.dat";
    const uint16_t value = 0xff00;

    // 写入
    {
        std::ofstream fout(filename, std::ios::binary);
        if (!fout.write(reinterpret_cast<const char*>(&value), sizeof(value))) {
            std::cerr << "Failed to write " << filename << '\n';
            return EXIT_FAILURE;
        }
    }

    // 读取第一个字节
    uint8_t first_byte = 0;
    {
        std::ifstream fin(filename, std::ios::binary);
        if (!fin.read(reinterpret_cast<char*>(&first_byte), sizeof(first_byte))) {
            std::cerr << "Failed to read " << filename << '\n';
            return EXIT_FAILURE;
        }
    }

    // 大端：高字节（0xff）在低地址，即文件首字节为 0xff
    if (first_byte == 0xff) {
        std::cout << "BIG ENDIAN\n";
    } else {
        std::cout << "LITTLE ENDIAN\n";
    }
    return 0;
}