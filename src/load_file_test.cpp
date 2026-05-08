#include "../include/FAT16.hpp"

int main()
{
    FAT16 test("empty.img");
    test.load_file("PORN.MP4", "PORN.MP4");
    test.export_file("PORN.MP4", "EXPORT.MP4");
}