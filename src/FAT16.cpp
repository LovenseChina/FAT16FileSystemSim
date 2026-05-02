#include "../include/FAT16.hpp"
#include "../include/BlockDevice.hpp"
#include <iomanip>

FAT16::FAT16(const std::string &disk_img) : disk_name(disk_img)
{
    //  读取DBR
    std::ifstream disk_in(disk_img, std::ios_base::binary);
    if (!disk_in.is_open())
    {
        std::cerr << "Cannot open disk image \"" << disk_img
                  << "\"\nAobrt.\n";
        exit(EXIT_FAILURE);
    }
    disk_in.read(reinterpret_cast<char *>(&this->DBR_512), 512); //  如果 BPB_BytsPerSec > 512 则存在此域，全部置零，故只需读入起始512字节

    // 检验每个扇区的字节数的合法性
    uint16_t BPS = this->DBR_512._BPB_.BPB_BytsPerSec;
    if (!(BPS == 512 || BPS == 1024 || BPS == 2048 || BPS == 4096))
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": BPB_BytsPerSec = "
                  << BPS << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    // 检验保留区域的扇区数量
    if (this->DBR_512._BPB_.BPB_RsvdSecCnt == 0)
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": BPB_RsvdSecCnt = "
                  << this->DBR_512._BPB_.BPB_RsvdSecCnt << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    //  检验根目录中的条目数
    if (32 * this->DBR_512._BPB_.BPB_RootEntCnt % 2 != 0)
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": BPB_RootEntCnt = "
                  << this->DBR_512._BPB_.BPB_RootEntCnt << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    //  检验16位长度卷的总扇区数和32位长度卷的总扇区数
    uint32_t TS16 = this->DBR_512._BPB_.BPB_TotSec16,
             TS32 = this->DBR_512._BPB_.BPB_TotSec32;
    if (!((TS16 < 0x10000 && TS32 == 0) || (TS16 == 0 && TS32 >= 0x10000)))
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": BPB_TotSec16 = "
                  << TS16
                  << ", BPB_TotSec32 = "
                  << TS32
                  << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    //  检验单个FAT表占用的扇区数
    if (this->DBR_512._BPB_.BPB_FATSz16 == 0)
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": BPB_FATSz16 = "
                  << this->DBR_512._BPB_.BPB_FATSz16 << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    // 检验用于0x13中断的驱动器号
    if (!(this->DBR_512._BS_END_.BS_DrvNum == 0x80 || this->DBR_512._BS_END_.BS_DrvNum == 0x00))
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": BS_DrvNum = "
                  << this->DBR_512._BS_END_.BS_DrvNum << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    // 检验保留位
    if (this->DBR_512._BS_END_.BS_Reserved1 != 0)
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": BS_Reserved1 = "
                  << this->DBR_512._BS_END_.BS_Reserved1 << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    // 检验启动扇区的完整性的签名（松散检测）
    if (DBR_512._BS_END_.BS_BootSig != 0x28 && DBR_512._BS_END_.BS_BootSig != 0x29)
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": BS_BootSig = "
                  << DBR_512._BS_END_.BS_BootSig << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    // 检验空余（小于512字节）
    for (int i = 0; i < 448; ++i)
    {
        if (this->DBR_512.DBR_Zero[i] != 0)
        {
            std::cerr << "Invalid disk image \"" << disk_img
                      << "\": Dirty DBR " << "\nAbort.\n";
            exit(EXIT_FAILURE);
        }
    }

    // 检验校验位
    if (this->DBR_512.Signature_word != 0xAA55)
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": Signature_word = "
                  << this->DBR_512.Signature_word << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    // 检验空余（大于512字节）
    if (this->DBR_512._BPB_.BPB_BytsPerSec > 512)
    {
        int16_t rest_zero_bytes = this->DBR_512._BPB_.BPB_BytsPerSec - 512;
        std::vector<uint8_t> datas(rest_zero_bytes);
        disk_in.read(reinterpret_cast<char *>(datas.data()), rest_zero_bytes);
        for (int16_t i = 0; i < rest_zero_bytes; ++i)
        {
            if (datas[i] != 0)
            {
                std::cerr << "Invalid disk image \"" << disk_img
                          << "\": Dirty DBR " << "\nAbort.\n";
                exit(EXIT_FAILURE);
            }
        }
    }

    //  构造FAT表
    uint32_t fat16_table_bytes = static_cast<uint32_t>(this->DBR_512._BPB_.BPB_FATSz16) * static_cast<uint32_t>(this->DBR_512._BPB_.BPB_BytsPerSec);
    this->fat_table.resize(fat16_table_bytes / sizeof(FAT16_ENTRY));
    disk_in.read(reinterpret_cast<char *>(this->fat_table.data()), fat16_table_bytes);
    disk_in.seekg(static_cast<uint32_t>(this->DBR_512._BPB_.BPB_FATSz16) * static_cast<uint32_t>(this->DBR_512._BPB_.BPB_BytsPerSec), std::ios::cur); //  忽略冗余FAT

    //  FAT[0]检验
    int16_t sign_ext_media = static_cast<int8_t>(this->DBR_512._BPB_.BPB_Media);
    if (static_cast<uint16_t>(sign_ext_media) != this->fat_table[0])
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": BPB_Media = "
                  << this->DBR_512._BPB_.BPB_Media
                  << ", FAT[0] "
                  << this->fat_table[0]
                  << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    // 检测簇数是否为 FAT16 规定合法值
    uint32_t total_clus = this->get_total_clusters();
    if (!(total_clus >= 4085 && total_clus < 65525))
    {
        std::cerr << "\"" << disk_img
                  << "\"may be FAT12 or FAT32 image, total clusters = "
                  << total_clus << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    //  构造根目录表
    uint32_t root_table_bytes = 32 * static_cast<uint32_t>(this->DBR_512._BPB_.BPB_RootEntCnt);
    this->root_entry_table.resize(this->DBR_512._BPB_.BPB_RootEntCnt);
    disk_in.read(reinterpret_cast<char *>(this->root_entry_table.data()), root_table_bytes);

    //  检测根目录中卷标文件与 DBR 中卷标的一致性（严格检测）
    if (this->DBR_512._BS_END_.BS_BootSig == 0x29)
    {
        std::vector<DIR_ENTRY>::iterator VolLab_it;
        for (VolLab_it = this->root_entry_table.begin(); VolLab_it != this->root_entry_table.end(); ++VolLab_it)
        {
            if (VolLab_it->DIR_Attr == 0x08)
            {
                break;
            }
        }
        if (VolLab_it == this->root_entry_table.end())
        {
            std::cerr << "Invalid disk image \"" << disk_img
                      << "\": Without volume label file in the root directory"
                      << "\nAbort.\n";
            exit(EXIT_FAILURE);
        }
        for (int i = 0; i < 11; ++i)
        {
            if (this->DBR_512._BS_END_.BS_VolLab[i] != VolLab_it->DIR_Name[i])
            {
                std::cerr << "Invalid disk image \"" << disk_img
                          << "\": BS_VolLab[" << i << "] = 0x"
                          << std::setw(2) << std::setfill('0') << std::hex
                          << static_cast<uint32_t>(this->DBR_512._BS_END_.BS_VolLab[i])
                          << ", DIR_Name[" << i << "] = 0x"
                          << std::setw(2) << std::setfill('0') << std::hex
                          << VolLab_it->DIR_Name[i]
                          << "\nAbort.\n";
                exit(EXIT_FAILURE);
            }
        }
    }

    std::cout << "Disk meta data all check clear.\n"
              << "FAT table constructed.\n"
              << "Root directory constructed.\n";

    //  关闭文件流
    disk_in.close();

    //  初始当前路径为根目录 /
    this->pwd = "/";

    //  依据DBR数据构造块设备
    uint32_t total_sectors = this->DBR_512._BPB_.BPB_TotSec16 > 0 ? this->DBR_512._BPB_.BPB_TotSec16 : this->DBR_512._BPB_.BPB_TotSec32;
    this->device.reset(new FileBackedBlockDevice(disk_img, this->DBR_512._BPB_.BPB_BytsPerSec, total_sectors));

    std::cout << "\"" << disk_img << "\" has mounted.\n";
}

FAT16::~FAT16()
{
    if (this->device)
    {
        this->device->flush_to_file();
    }
}

uint32_t FAT16::get_total_clusters() const
{
    //  获取总扇区数
    uint32_t total_sectors = this->DBR_512._BPB_.BPB_TotSec16 > 0 ? this->DBR_512._BPB_.BPB_TotSec16 : this->DBR_512._BPB_.BPB_TotSec32;
    //  计算根目录所占扇区
    uint32_t total_root_ent_sectors = this->DBR_512._BPB_.BPB_RootEntCnt * 32 /
                                      this->DBR_512._BPB_.BPB_BytsPerSec;
    // 计算总数据/子目录扇区数
    uint32_t total_data_sectors = total_sectors -
                                  this->DBR_512._BPB_.BPB_RsvdSecCnt -
                                  this->DBR_512._BPB_.BPB_FATSz16 * this->DBR_512._BPB_.BPB_NumFATs -
                                  total_root_ent_sectors;
    //  计算返回总簇数
    return total_data_sectors / this->DBR_512._BPB_.BPB_SecPerClus;
}