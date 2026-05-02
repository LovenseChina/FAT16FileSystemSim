#include <cstdint>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <string>
#include <vector>

/**
 * @brief 分离自 FAT16.cpp 中 FAT16 构造函数中的 FAT16 镜像的验证代码
 *
 * @cite https://ysos.gzti.me/
 * @author Tang Jung-Chi
 */

#pragma pack(push, 1)
/**
 * @brief 在 FAT 格式的卷的首扇区
 *
 * - 包含jmp指令和OEM信息以及BPB和拓展BPB（BS）
 */
struct BPB
{
    uint16_t BPB_BytsPerSec; //  每个扇区的字节数    2B
    uint8_t BPB_SecPerClus;  //  每个簇的扇区数量    1B
    uint16_t BPB_RsvdSecCnt; //  保留区域的扇区数量  2B  comment:保留扇区在FAT16中就是DBR所在扇区
    uint8_t BPB_NumFATs;     //  FAT表数量   1B
    uint16_t BPB_RootEntCnt; //  根目录中的条目数    2B
    uint16_t BPB_TotSec16;   //  16位长度卷的总扇区数    2B
    uint8_t BPB_Media;       //  设备的类型  1B
    uint16_t BPB_FATSz16;    //  单个FAT表占用的扇区数   2B
    uint16_t BPB_SecPerTrk;  //  每个扇区的磁道数    2B
    uint16_t BPB_NumHeads;   //  磁头数量    2B
    uint32_t BPB_HiddSec;    //  分区前隐藏的扇区数  4B
    uint32_t BPB_TotSec32;   //  32位长度卷的总扇区数    4B
};

struct BS_END
{
    uint8_t BS_DrvNum;        //  驱动器号    1B
    uint8_t BS_Reserved1;     //  保留位  1B
    uint8_t BS_BootSig;       //  检验启动扇区的完整性的签名  1B
    uint32_t BS_VolID;        //  卷的序列号  4B
    uint8_t BS_VolLab[11];    //  卷标    11B
    uint8_t BS_FilSysType[8]; //  描述文件系统类型    8B
};

struct DBR
{
    uint8_t BS_jmpBoot[3]; //  跳转到启动代码处执行的指令  3B
    uint8_t BS_OEMName[8]; //  OEM厂商的名称 8B
    BPB _BPB_;
    BS_END _BS_END_;
    uint8_t DBR_Zero[448];   //  空余，置零  448B
    uint16_t Signature_word; //  校验位  2B
};
#pragma pack(pop)

uint32_t get_total_clusters(const DBR & DBR_512);

int main(int argc, char *argv[])
{
    if (argc != 2 && argc != 3)
    {
        std::cerr << "USAGE 1: " << argv[0] << " <FAT16_disk_img_name>\n"
                  << "USAGE 2: " << argv[0] << " -d <FAT16_disk_img_name>";
        exit(EXIT_FAILURE);
    }
    if (argc == 3)
    {
        std::string param = argv[1];
        if (param != "-d")
        {
            std::cerr << "Invalid parameter.\n";
            exit(EXIT_FAILURE);
        }
    }
    std::string disk_img;
    if (argc == 2)
    {
        disk_img = argv[1];
    }
    else
    {
        disk_img = argv[2];
    }
    DBR DBR_512;
    //  读取DBR
    std::ifstream disk_in(disk_img, std::ios_base::binary);
    if (!disk_in.is_open())
    {
        std::cerr << "Cannot open disk image \"" << disk_img
                  << "\"\nAobrt.\n";
        exit(EXIT_FAILURE);
    }
    disk_in.read(reinterpret_cast<char *>(&DBR_512), 512); //  如果 BPB_BytsPerSec > 512 则存在此域，全部置零，故只需读入起始512字节

    // 检验每个扇区的字节数的合法性
    uint16_t BPS = DBR_512._BPB_.BPB_BytsPerSec;
    if (!(BPS == 512 || BPS == 1024 || BPS == 2048 || BPS == 4096))
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": BPB_BytsPerSec = "
                  << BPS << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    // 检验保留区域的扇区数量
    if (DBR_512._BPB_.BPB_RsvdSecCnt == 0)
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": BPB_RsvdSecCnt = "
                  << DBR_512._BPB_.BPB_RsvdSecCnt << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    //  检验根目录中的条目数
    if (32 * DBR_512._BPB_.BPB_RootEntCnt % 2 != 0)
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": BPB_RootEntCnt = "
                  << DBR_512._BPB_.BPB_RootEntCnt << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    //  检验16位长度卷的总扇区数和32位长度卷的总扇区数
    uint32_t TS16 = DBR_512._BPB_.BPB_TotSec16,
             TS32 = DBR_512._BPB_.BPB_TotSec32;
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
    if (DBR_512._BPB_.BPB_FATSz16 == 0)
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": BPB_FATSz16 = "
                  << DBR_512._BPB_.BPB_FATSz16 << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    // 检验用于0x13中断的驱动器号
    if (!(DBR_512._BS_END_.BS_DrvNum == 0x80 || DBR_512._BS_END_.BS_DrvNum == 0x00))
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": BS_DrvNum = "
                  << DBR_512._BS_END_.BS_DrvNum << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    // 检验保留位
    if (DBR_512._BS_END_.BS_Reserved1 != 0)
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": BS_Reserved1 = "
                  << DBR_512._BS_END_.BS_Reserved1 << "\nAbort.\n";
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
    if (argc == 2)
    {
        for (int i = 0; i < 448; ++i)
        {
            if (DBR_512.DBR_Zero[i] != 0)
            {
                std::cerr << "Invalid disk image \"" << disk_img
                          << "\": Dirty DBR " << "\nAbort.\n";
                exit(EXIT_FAILURE);
            }
        }
    }

    // 检验校验位
    if (DBR_512.Signature_word != 0xAA55)
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": Signature_word = "
                  << DBR_512.Signature_word << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    // 检验空余（大于512字节）
    if (argc == 2)
    {
        if (DBR_512._BPB_.BPB_BytsPerSec > 512)
        {
            int16_t rest_zero_bytes = DBR_512._BPB_.BPB_BytsPerSec - 512;
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
    }

    // FAT[0]检验
    int16_t media = static_cast<int8_t>(DBR_512._BPB_.BPB_Media), fat_0;
    disk_in.read(reinterpret_cast<char *>(&fat_0), 2);
    if (media != fat_0)
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": BPB_Media = "
                  << DBR_512._BPB_.BPB_Media
                  << ", FAT[0] "
                  << fat_0
                  << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    // 检测簇数是否为 FAT16 规定合法值
    uint32_t total_clus = get_total_clusters(DBR_512);
    if (!(total_clus >= 4085 && total_clus < 65525))
    {
        std::cerr << "\"" << disk_img
                  << "\"may be FAT12 or FAT32 image, total clusters = "
                  << total_clus << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    disk_in.close();
    std::cout << "FAT16 disk image is valid.\n";
    return 0;
}

uint32_t get_total_clusters(const DBR & DBR_512)
{   
    //  获取总扇区数
    uint32_t total_sectors = DBR_512._BPB_.BPB_TotSec16 > 0 ?
        DBR_512._BPB_.BPB_TotSec16 :
        DBR_512._BPB_.BPB_TotSec32;
    //  计算根目录所占扇区
    uint32_t total_root_ent_sectors = DBR_512._BPB_.BPB_RootEntCnt * 32 /
        DBR_512._BPB_.BPB_BytsPerSec;
    // 计算总数据/子目录扇区数
    uint32_t total_data_sectors = total_sectors -
        DBR_512._BPB_.BPB_RsvdSecCnt -
        DBR_512._BPB_.BPB_FATSz16 * DBR_512._BPB_.BPB_NumFATs -
        total_root_ent_sectors;
    //  计算返回总簇数
    return total_data_sectors / DBR_512._BPB_.BPB_SecPerClus;
}