#include "../include/SimpleFs.hpp"
#include <iostream>
#include <string>
#include <sstream>
#include <vector>

void print_help()
{
    std::cout << "Commands:\n"
              << "  format                - Format the virtual disk\n"
              << "  ls                    - List files\n"
              << "  touch <filename>      - Create empty file\n"
              << "  write <filename> <text> - Write text to file (overwrites)\n"
              << "  cat <filename>        - Show file content\n"
              << "  rm <filename>         - Delete file\n"
              << "  help                  - Show this help\n"
              << "  exit                  - Exit program\n";
}

int main(int argc, char *argv[])
{
    std::string disk_image = "virtual.disk";
    if (argc >= 2)
    {
        disk_image = argv[1];
    }

    SimpleFS fs(disk_image);

    std::cout << "Simple File System Simulator (C++11)\n";
    std::cout << "Disk image: " << disk_image << "\n";
    print_help();

    std::string line;
    while (true)
    {
        std::cout << "> ";
        std::getline(std::cin, line);
        if (line.empty())
            continue;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "exit")
        {
            break;
        }
        else if (cmd == "format")
        {
            fs.format();
        }
        else if (cmd == "ls")
        {
            fs.ls();
        }
        else if (cmd == "touch")
        {
            std::string name;
            iss >> name;
            if (name.empty())
            {
                std::cout << "Usage: touch <filename>\n";
            }
            else
            {
                fs.touch(name);
            }
        }
        else if (cmd == "write")
        {
            std::string name;
            iss >> name;
            if (name.empty())
            {
                std::cout << "Usage: write <filename> <text>\n";
                continue;
            }
            // 读取剩余全部作为文件内容
            std::string content;
            std::getline(iss, content);
            if (!content.empty() && content[0] == ' ')
                content.erase(0, 1);
            fs.write_file(name, content);
        }
        else if (cmd == "cat")
        {
            std::string name;
            iss >> name;
            if (name.empty())
            {
                std::cout << "Usage: cat <filename>\n";
            }
            else
            {
                fs.cat(name);
            }
        }
        else if (cmd == "rm")
        {
            std::string name;
            iss >> name;
            if (name.empty())
            {
                std::cout << "Usage: rm <filename>\n";
            }
            else
            {
                fs.rm(name);
            }
        }
        else if (cmd == "help")
        {
            print_help();
        }
        else
        {
            std::cout << "Unknown command. Type 'help'.\n";
        }
    }

    fs.sync();
    std::cout << "Goodbye.\n";
    return 0;
}