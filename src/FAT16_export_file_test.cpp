#include "../include/FAT16.hpp"

int main()
{
    FAT16 test("empty.img");
    test.export_file("TEST.GIF", "export.gif");
    test.export_file("PORN.MP4", "porn.mp4");
}