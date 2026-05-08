#include "../include/FAT16.hpp"
#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <cstring>
#include <iomanip>

void print_help() {
    std::cout << "Available commands:\n"
              << "  ls       [path]                List directory contents\n"
              << "  cd       <path>                Change working directory\n"
              << "  pwd                            Print working directory\n"
              << "  mkdir    <path>                Create directory\n"
              << "  rmdir    <path>                Remove empty directory\n"
              << "  touch    <path>                Create empty file\n"
              << "  rm       <path>                Delete file\n"
              << "  cat      <path>                Print file contents to stdout\n"
              << "  write    <path> <content>      Write content to file (overwrite)\n"
              << "  import   <src> <dest>          Load host file into image\n"
              << "  export   <src> <dest>          Extract file from image to host\n"
              << "  sync                           Flush all changes to disk image\n"
              << "  exit                           Exit shell (auto sync on quit)\n"
              << "  help                           Show this message\n";
}

// 将8.3格式DIR_Name转换为可打印字符串（直接调用FAT16的成员）
std::string dirname_to_string(FAT16 &fat, const uint8_t *dir_name) {
    std::string s;
    fat.short_name_to_string(dir_name, s);
    return s.empty() ? "???" : s;
}

// 显示目录列表
void list_dir(FAT16 &fat, const std::string &path) {
    auto entries = fat.list_dir(path);
    if (entries.empty()) {
        std::cout << "(empty)\n";
        return;
    }
    std::cout << std::left
              << std::setw(12) << "Name"
              << std::setw(6)  << "Type"
              << std::setw(10) << "Size"
              << "\n";
    std::cout << std::string(28, '-') << "\n";
    for (const auto &e : entries) {
        std::string name = dirname_to_string(fat, e.DIR_Name);
        std::string type = (e.DIR_Attr & 0x10) ? "<DIR>" : "<FILE>";
        std::cout << std::left
                  << std::setw(12) << name
                  << std::setw(6)  << type
                  << std::setw(10) << e.DIR_FileSize
                  << "\n";
    }
}

// 简易命令解析：支持双引号字符串，其余空格分割
std::vector<std::string> tokenize(const std::string &line) {
    std::vector<std::string> args;
    std::string token;
    bool in_quotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char ch = line[i];
        if (ch == '"') {
            in_quotes = !in_quotes;
        } else if (ch == ' ' && !in_quotes) {
            if (!token.empty()) args.push_back(token);
            token.clear();
        } else {
            token += ch;
        }
    }
    if (!token.empty()) args.push_back(token);
    return args;
}

int main(int argc, char *argv[]) {
    std::string image_file = (argc > 1) ? argv[1] : "FAT16.img";
    std::cout << "Mounting image: " << image_file << "\n";

    FAT16 fat(image_file);   // 构造函数会完成验证和元数据加载

    std::cout << "FAT16 Shell (type 'help' for commands)\n";
    std::string line;
    while (true) {
        std::cout << "fat16:" << fat.pwd << "$ ";
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;
        auto args = tokenize(line);
        if (args.empty()) continue;

        std::string cmd = args[0];
        if (cmd == "exit") {
            break;
        } else if (cmd == "help") {
            print_help();
        } else if (cmd == "pwd") {
            fat.show_pwd();
        } else if (cmd == "ls") {
            std::string path = (args.size() > 1) ? args[1] : ".";
            list_dir(fat, path);
        } else if (cmd == "cd") {
            if (args.size() < 2) {
                std::cerr << "Usage: cd <path>\n";
            } else {
                fat.change_dir(args[1]);
            }
        } else if (cmd == "mkdir") {
            if (args.size() < 2) {
                std::cerr << "Usage: mkdir <path>\n";
            } else {
                fat.create_dir(args[1]);
            }
        } else if (cmd == "rmdir") {
            if (args.size() < 2) {
                std::cerr << "Usage: rmdir <path>\n";
            } else {
                fat.remove_dir(args[1]);
            }
        } else if (cmd == "touch") {
            if (args.size() < 2) {
                std::cerr << "Usage: touch <path>\n";
            } else {
                fat.create_file(args[1]);
            }
        } else if (cmd == "rm") {
            if (args.size() < 2) {
                std::cerr << "Usage: rm <path>\n";
            } else {
                fat.delete_file(args[1]);
            }
        } else if (cmd == "cat") {
            if (args.size() < 2) {
                std::cerr << "Usage: cat <path>\n";
            } else {
                std::vector<char> buf;
                if (fat.read_file(args[1], buf)) {
                    std::cout.write(buf.data(), buf.size());
                    std::cout << std::flush;
                }
            }
        } else if (cmd == "write") {
            if (args.size() < 3) {
                std::cerr << "Usage: write <path> <content>\n"
                          << "  (use quotes for spaces, e.g. write a.txt \"hello world\")\n";
            } else {
                std::vector<char> data(args[2].begin(), args[2].end());
                fat.write_file(args[1], data);
            }
        } else if (cmd == "import") {
            if (args.size() < 3) {
                std::cerr << "Usage: import <host-file> <image-path>\n";
            } else {
                fat.load_file(args[1], args[2]);
            }
        } else if (cmd == "export") {
            if (args.size() < 3) {
                std::cerr << "Usage: export <image-path> <host-file>\n";
            } else {
                fat.export_file(args[1], args[2]);
            }
        } else if (cmd == "sync") {
            fat.sync();
        } else {
            std::cerr << "Unknown command: " << cmd << "\n";
        }
    }

    // 正常退出会自动析构并 sync，但也可以显式调用一次
    std::cout << "Goodbye.\n";
    return 0;
}