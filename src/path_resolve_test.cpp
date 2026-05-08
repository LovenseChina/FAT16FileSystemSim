#include "../include/FAT16.hpp"

int main()
{
    FAT16 test("empty.img");
    std::cout << "Testing...\n";
    FAT16::PATH_RESULT path_result_info;
    if (!test.resolve_path("/TEST1.TXT", path_result_info))
    {
        std::cerr << "resolve_path() failed!\n";
    }
    else if (path_result_info.exists)
    {
        std::string name;
        test.short_name_to_string(path_result_info.entry.DIR_Name, name);
        if (name != "")
        {
            std::cout << "destination: " << name << '\n';
        }
        else
        {
            std::cout << "destination: /\n";
        }
    }
    else
    {
        std::cerr << "parent direcotry resolve success but destnation failed.\nparent: " << path_result_info.parent_filename << "\n";
    }
    if (!test.create_file("/DIR1/DUP.TXT"))
    {
        std::cerr << "file created faild!\n";
    }
    else
    {
        std::cout << "file created!\n";
    }
    if (!test.create_file("/R_DUP.TXT"))
    {
        std::cerr << "file created faild!\n";
    }
    else
    {
        std::cout << "file created!\n";
    }
    if (!test.create_file("/TEST.TXT"))
    {
        std::cerr << "file created faild!\n";
    }
    else
    {
        std::cout << "file created!\n";
    }
}