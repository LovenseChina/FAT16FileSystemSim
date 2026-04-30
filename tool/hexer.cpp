#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <cctype>

constexpr int32_t DataBlockSize = 8;

int main(int argc, const char *argv[])
{
    if (argc != 2)
    {
        std::cerr << "USAGE: " << argv[0] << " <file_name>\n";
        exit(EXIT_FAILURE);
    }
    std::ifstream fin(argv[1], std::ios_base::binary);
    if (!fin.good())
    {
        std::cerr << "Failed in opening \"" << argv[1] << "\"!\n";
        exit(EXIT_FAILURE);
    }
    uint8_t buffer[DataBlockSize];
    while (fin.read(reinterpret_cast<char *>(buffer), DataBlockSize))
    {
        for (int32_t i = 0; i < DataBlockSize; ++i)
        {
            std::cout << std::hex << std::showbase << std::setw(4) << std::right
                      << static_cast<uint32_t>(buffer[i]) << ' ';
        }
        std::cout << " < hex | ascii > ";
        for (int32_t i = 0; i < DataBlockSize; ++i)
        {   
            if (isprint(buffer[i]))
            {
                std::cout << buffer[i];
            }
            else
            {
                std::cout << ' ';
            }
        }
        std::cout << '\n';
    }
    std::streamsize n = fin.gcount();
    if (n > 0)
    {
        for (std::streamsize i = 0; i < DataBlockSize; ++i)
        {   
            if (i < n)
            {
                std::cout << std::hex << std::showbase << std::setw(4) << std::right
                          << static_cast<uint32_t>(buffer[i]) << ' ';
            }
            else
            {
                std::cout << std::hex << std::showbase << std::setw(4) << std::right
                      << "    " << ' ';
            }
        }
        std::cout << " < hex | ascii > ";
        for (std::streamsize i = 0; i < n; ++i)
        {   
            if (isprint(buffer[i]))
            {
                std::cout << buffer[i];
            }
            else
            {
                std::cout << ' ';
            }
        }
        std::cout << '\n';
    }
    return 0;
}