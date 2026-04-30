#include "../include/FAT16DiskGenerator.hpp"
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <algorithm>

/********** 公有常量定义 **********/
/*
constexpr int16_t FAT16DiskGenerator::MIN = 2;
constexpr int16_t FAT16DiskGenerator::MAX = 8191;
constexpr int16_t FAT16DiskGenerator::FAT_0 = 0xFFF0;
constexpr int16_t FAT16DiskGenerator::FAT_1 = 0xFFFF;    // 表示正常卸载的FAT16磁盘

constexpr int16_t FAT16DiskGenerator::BPB_BYTS_PER_SEC = 512;
constexpr int8_t FAT16DiskGenerator::BPB_SEC_PER_CLUS = 8;
constexpr int16_t FAT16DiskGenerator::BPB_RSVD_SEC_CNT = 1;
constexpr int8_t FAT16DiskGenerator::BPB_NUM_FATS = 2;
constexpr int16_t FAT16DiskGenerator::BPB_ROOT_ENT_CNT = 512;
constexpr int16_t FAT16DiskGenerator::BPB_TOT_SEC_16 = 0;
constexpr int8_t FAT16DiskGenerator::BPB_MEDIA = 0xF0;   //  可移动磁盘
constexpr int16_t FAT16DiskGenerator::BPB_FAT_SZ_16 = 32;
constexpr int16_t FAT16DiskGenerator::BPB_SEC_PER_TRK = 0;   //  0x13中断，无关字段取0即可
constexpr int16_t FAT16DiskGenerator::BPB_NUM_HEADS = 0; //  0x13中断，无关字段取0即可
constexpr int32_t FAT16DiskGenerator::BPB_HIDD_SEC = 0;  //  0x13中断，单分区下无用，取0即可
constexpr int32_t FAT16DiskGenerator::BPB_TOT_SEC_32 = 65617;    //  1 + 32 + 32 + 32 + 65520

constexpr int8_t FAT16DiskGenerator::BS_DRV_NUM = 0; //  0x13中断，无关字段取0即可
constexpr int8_t FAT16DiskGenerator::BS_RESERVED_1 = 0;
constexpr int8_t FAT16DiskGenerator::BS_BOOT_SIG = 0x29;    //  检验启动扇区的完整性的签名，提供了 BS_VolID、BS_VolLab 等扩展字段，则 BS_BootSig 必须为 0x29
constexpr int32_t FAT16DiskGenerator::BS_VOl_ID = 0; //  卷的序列号，可忽略

constexpr int16_t FAT16DiskGenerator::SIGNATURE_WORD = 0xAA55; */

/********** 成员函数定义 **********/
FAT16DiskGenerator::FAT16DiskGenerator(const std::string & disk_name) :
    fat_table(8192, 0),
    root_entry_table(512),
    disk_name(disk_name),
    disk_out(disk_name, std::ios_base::binary)
{}

FAT16DiskGenerator::~FAT16DiskGenerator()
{
    if (this->disk_out.is_open())
    {
        this->disk_out.close();
    }
}

void FAT16DiskGenerator::gen_ds_data()
{   
    // 整体清零 DBR，防止未初始化垃圾值
    memset(&this->first_sector, 0, sizeof(this->first_sector));

    /**
     * @brief BS_jmpBoot为一条机器指令，必须手动按小端序依字节填入
     */
    this->first_sector.BS_jmpBoot[0] = 0xEB;
    this->first_sector.BS_jmpBoot[1] = 0x00;
    this->first_sector.BS_jmpBoot[2] = 0x90;
    
    /**
     * @brief OEM厂商的名称为字符串
     */
    memset(this->first_sector.BS_OEMName, ' ', sizeof(this->first_sector.BS_OEMName));
    memcpy(this->first_sector.BS_OEMName, "__TJC__", 7);    //  不复制'\0'

    /**
     * @brief BPB 相关字段直接填入
     */
    this->first_sector._BPB_.BPB_BytsPerSec = BPB_BYTS_PER_SEC;
    this->first_sector._BPB_.BPB_SecPerClus = BPB_SEC_PER_CLUS;
    this->first_sector._BPB_.BPB_RsvdSecCnt = BPB_RSVD_SEC_CNT;
    this->first_sector._BPB_.BPB_NumFATs = BPB_NUM_FATS;
    this->first_sector._BPB_.BPB_RootEntCnt = BPB_ROOT_ENT_CNT;
    this->first_sector._BPB_.BPB_TotSec16 = BPB_TOT_SEC_16;
    this->first_sector._BPB_.BPB_Media = BPB_MEDIA;
    this->first_sector._BPB_.BPB_FATSz16 = BPB_FAT_SZ_16;
    this->first_sector._BPB_.BPB_SecPerTrk = BPB_SEC_PER_TRK;
    this->first_sector._BPB_.BPB_NumHeads = BPB_NUM_HEADS;
    this->first_sector._BPB_.BPB_HiddSec = BPB_HIDD_SEC;
    this->first_sector._BPB_.BPB_TotSec32 = BPB_TOT_SEC_32;

    /**
     * @brief BS_END 相关字段直接填入
     */
    this->first_sector._BS_END_.BS_DrvNum = BS_DRV_NUM;
    this->first_sector._BS_END_.BS_Reserved1 = BS_RESERVED_1;
    this->first_sector._BS_END_.BS_BootSig = BS_BOOT_SIG;
    this->first_sector._BS_END_.BS_VolID = BS_VOl_ID;
    memset(this->first_sector._BS_END_.BS_VolLab, ' ', sizeof(this->first_sector._BS_END_.BS_VolLab));
    memcpy(this->first_sector._BS_END_.BS_VolLab, "TJC vdisk", 9);      //  不复制'\0'
    memset(this->first_sector._BS_END_.BS_FilSysType, ' ', sizeof(this->first_sector._BS_END_.BS_FilSysType));
    memcpy(this->first_sector._BS_END_.BS_FilSysType, "FAT16", 5);      //  不复制'\0'
    
    memset(this->first_sector.DBR_Zero, 0, 448);
    this->first_sector.Signature_word = SIGNATURE_WORD;

    //  FAT表项除FAT[0]和FAT[1]由构造函数直接初始化
    this->fat_table[0] = FAT_0;
    this->fat_table[1] = FAT_1;

    //  根目录初始化
    std::for_each(this->root_entry_table.begin(), this->root_entry_table.end(), [](DIR_ENTRY & ent) { memset(&ent, 0, sizeof(ent)); });
}

bool FAT16DiskGenerator::put()
{
    if (!this->disk_out.is_open())
    {
        std::cerr << "\"" << this->disk_name << "\"" << "failed to open!\n";
        exit(EXIT_FAILURE);
    }
    this->disk_out.write(reinterpret_cast<const char *>(&this->first_sector), BPB_BYTS_PER_SEC);    //  写入DBR，1个扇区
    this->disk_out.write(reinterpret_cast<const char *>(this->fat_table.data()), BPB_BYTS_PER_SEC * BPB_FAT_SZ_16); //  写入FAT1，32个扇区
    this->disk_out.write(reinterpret_cast<const char *>(this->fat_table.data()), BPB_BYTS_PER_SEC * BPB_FAT_SZ_16); //  写入FAT2，32个扇区
    this->disk_out.write(reinterpret_cast<const char *>(this->root_entry_table.data()), BPB_ROOT_ENT_CNT * sizeof(FAT16DiskGenerator::DIR_ENTRY));  //  写入根目录，32个扇区

    /**
     * @brief 生成空簇
     */
    char empty_sector[BPB_BYTS_PER_SEC] = { 0 };
    for (int16_t i = MIN; i <= MAX; ++i)
    {
        this->disk_out.write(empty_sector, BPB_BYTS_PER_SEC * BPB_SEC_PER_CLUS);
    }
    return true;
}