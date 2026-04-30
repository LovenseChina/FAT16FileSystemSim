#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <cctype>
#include <string>

constexpr int32_t DataBlockSize = 8;

std::ostream &show_hex_byte(std::ostream &out, uint8_t ch)
{
    out << "0x" << std::setw(2) << std::setfill('0')
        << std::hex << static_cast<uint32_t>(ch) << ' ';
    return out;
}

int main(int argc, const char *argv[])
{
    if (argc != 2 && argc != 3)
    {
        std::cerr << "USAGE 1: " << argv[0] << " <file_name>\n"
                  << "USAGE 2: " << argv[0] << " <file_name> -fOnly";
        exit(EXIT_FAILURE);
    }
    if (argc == 3)
    {
        std::string argv2 = argv[2];
        if (argv2 != "-fOnly")
        {
            std::cerr << "USAGE 1: " << argv[0] << " <file_name>\n"
                      << "USAGE 2: " << argv[0] << " <file_name> -fOnly";
            exit(EXIT_FAILURE);
        }
    }
    std::string out_file_name = argv[1];
    out_file_name += "_hex.txt";
    std::ofstream fout(out_file_name);
    if (!fout.is_open())
    {
        std::cerr << "Failed in creating \"" << out_file_name << "\"\n";
        exit(EXIT_FAILURE);
    }
    std::ifstream fin(argv[1], std::ios_base::binary);
    if (!fin.is_open())
    {
        std::cerr << "Failed in opening \"" << argv[1] << "\"!\n";
        exit(EXIT_FAILURE);
    }
    uint8_t buffer[DataBlockSize];
    while (fin.read(reinterpret_cast<char *>(buffer), DataBlockSize))
    {
        for (int32_t i = 0; i < DataBlockSize; ++i)
        {
            show_hex_byte(fout, buffer[i]);
            if (argc == 2)
            {
                show_hex_byte(std::cout, buffer[i]);
            }
        }
        fout << " < hex | ascii > ";
        if (argc == 2)
        {
            std::cout << " < hex | ascii > ";
        }
        for (int32_t i = 0; i < DataBlockSize; ++i)
        {
            if (isprint(buffer[i]))
            {
                fout << buffer[i];
                if (argc == 2)
                {
                    std::cout << buffer[i];
                }
            }
            else
            {
                fout << ' ';
                if (argc == 2)
                {
                    std::cout << ' ';
                }
            }
        }
        fout << '\n';
        if (argc == 2)
        {
            std::cout << '\n';
        }
    }
    std::streamsize n = fin.gcount();
    if (n > 0)
    {
        for (std::streamsize i = 0; i < DataBlockSize; ++i)
        {
            if (i < n)
            {
                show_hex_byte(fout, buffer[i]);
                if (argc == 2)
                {
                    show_hex_byte(std::cout, buffer[i]);
                }
            }
            else
            {
                fout << std::setw(4) << std::right
                     << "    " << ' ';
                if (argc == 2)
                {
                    std::cout << std::setw(4) << std::right
                              << "    " << ' ';
                }
            }
        }
        fout << " < hex | ascii > ";
        if (argc == 2)
        {
            std::cout << " < hex | ascii > ";
        }
        for (std::streamsize i = 0; i < n; ++i)
        {
            if (isprint(buffer[i]))
            {
                fout << buffer[i];
                if (argc == 2)
                {
                    std::cout << buffer[i];
                }
            }
            else
            {
                fout << ' ';
                if (argc == 2)
                {
                    std::cout << ' ';
                }
            }
        }
        fout << '\n';
        if (argc == 2)
        {
            std::cout << '\n';
        }
    }
    fin.close();
    fout.close();
    std::cout << "\nReadable hex file \"" << out_file_name << "\" created successfully.\n";
    return 0;
}