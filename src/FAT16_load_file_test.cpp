#include "../include/FAT16.hpp"

int main()
{
    FAT16 test("empty.img");
    test.load_file("TEST.BIN", "TEST.BIN");
    test.export_file("TEST.BIN", "EXPORT.exe");
}