#include "../include/FAT16DiskGenerator.hpp"

int main()
{
    FAT16DiskGenerator disk_instance;
    disk_instance.gen_ds_data();
    disk_instance.put();
    return 0;
}