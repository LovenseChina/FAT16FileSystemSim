/**
 * @brief 
 * 
 * FAT16 虚拟磁盘生成器
 * inspired by YatSunOS v2 Tutorial
 * 
 * @details
 * 
 * - 一个数据块为一个扇区，大小为512B
 * - 每簇8扇区
 * - 一个分区就是一个卷（不支持多卷管理）
 * - 虚拟磁盘约32MB大小，含文件系统数据结构在内共65520个扇区
 * 
 * @cite https://ysos.gzti.me/
 * @author Tang Jung-Chi
 */

#include <cstdint>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>

/**
 * @brief 类声明
 */
class FAT16DiskGenerator
{
public:
/********** FAT_ENTRY 取值范围以及FAT[0]和FAT[1]常量 **********/
    static constexpr int16_t MIN = 2;
    static constexpr int16_t MAX = 8191;
    static constexpr int16_t FAT_0 = 0xFFF0;
    static constexpr int16_t FAT_1 = 0xFFFF;    // 表示正常卸载的FAT16磁盘

/********** BPB 填充用常量 **********/

    static constexpr int16_t BPB_BYTS_PER_SEC = 512;
    static constexpr int8_t BPB_SEC_PER_CLUS = 8;
    static constexpr int16_t BPB_RSVD_SEC_CNT = 1;
    static constexpr int8_t BPB_NUM_FATS = 2;
    static constexpr int16_t BPB_ROOT_ENT_CNT = 512;
    static constexpr int16_t BPB_TOT_SEC_16 = 0;
    static constexpr int8_t BPB_MEDIA = 0xF0;   //  可移动磁盘
    static constexpr int16_t BPB_FAT_SZ_16 = 32;
    static constexpr int16_t BPB_SEC_PER_TRK = 0;   //  0x13中断，无关字段取0即可
    static constexpr int16_t BPB_NUM_HEADS = 0; //  0x13中断，无关字段取0即可
    static constexpr int32_t BPB_HIDD_SEC = 0;  //  0x13中断，单分区下无用，取0即可
    static constexpr int32_t BPB_TOT_SEC_32 = 65617;    //  1 + 32 + 32 + 32 + 65520

/********** BS_END 填充用常量 **********/

    static constexpr int8_t BS_DRV_NUM = 0; //  0x13中断，无关字段取0即可
    static constexpr int8_t BS_RESERVED_1 = 0;
    static constexpr int8_t BS_BOOT_SIG = 0x29;    //  检验启动扇区的完整性的签名，提供了 BS_VolID、BS_VolLab 等扩展字段，则 BS_BootSig 必须为 0x29
    static constexpr int32_t BS_VOl_ID = 0; //  卷的序列号，可忽略

/********** DBR 签名填充用常量 **********/
    static constexpr int16_t SIGNATURE_WORD = 0xAA55;

/********** 公有成员函数 **********/

    /**
     * @brief Construct a new FAT16DiskGenerator object
     * 
     * @param disk_name 欲创建FAT16虚拟磁盘文件名
     */
    FAT16DiskGenerator(const std::string & disk_name = "FAT16.img");
    FAT16DiskGenerator(const FAT16DiskGenerator &) = delete;
    FAT16DiskGenerator & operator=(const FAT16DiskGenerator &) = delete;
    
    /**
     * @brief Destroy the FAT16DiskGenerator object
     * 
     * - 进行一些扫尾工作，如关闭文件流等
     */
    ~FAT16DiskGenerator();

    /**
     * @brief 生成FAT16的数据结构的数据内容
     */
    void gen_ds_data();

    /**
     * @brief 将FAT16的数据结构写入虚拟磁盘文件
     * 
     * @return true 成功写入FAT16数据结构到文件 
     * @return false 未成功写入文件
     */
    bool put();

private:
/********** FAT16保留扇区数据结构 **********/

    #pragma pack(push, 1)
    /**
     * @brief 在 FAT 格式的卷的首扇区
     * 
     * - 包含jmp指令和OEM信息以及BPB和拓展BPB（BS）
     */
    struct BPB
    {
        int16_t BPB_BytsPerSec; //  每个扇区的字节数    2B
        int8_t BPB_SecPerClus;  //  每个簇的扇区数量    1B
        int16_t BPB_RsvdSecCnt; //  保留区域的扇区数量  2B  comment:保留扇区在FAT16中就是DBR所在扇区
        int8_t BPB_NumFATs; //  FAT表数量   1B
        int16_t BPB_RootEntCnt; //  根目录中的条目数    2B
        int16_t BPB_TotSec16;   //  16位长度卷的总扇区数    2B
        int8_t BPB_Media;   //  设备的类型  1B
        int16_t BPB_FATSz16;    //  单个FAT表占用的扇区数   2B
        int16_t BPB_SecPerTrk;  //  每个扇区的磁道数    2B
        int16_t BPB_NumHeads;   //  磁头数量    2B
        int32_t BPB_HiddSec;    //  分区前隐藏的扇区数  4B
        int32_t BPB_TotSec32;   //  32位长度卷的总扇区数    4B
    };

    struct BS_END
    {
        int8_t BS_DrvNum;   //  驱动器号    1B
        int8_t BS_Reserved1;    //  保留位  1B
        int8_t BS_BootSig;  //  检验启动扇区的完整性的签名  1B
        int32_t BS_VolID;   //  卷的序列号  4B
        int8_t BS_VolLab[11];   //  卷标    11B
        int8_t BS_FilSysType[8];  //  描述文件系统类型    8B
    };
    
    struct DBR
    {
        int8_t BS_jmpBoot[3];   //  跳转到启动代码处执行的指令  3B
        int8_t BS_OEMName[8]; //  OEM厂商的名称 8B
        BPB _BPB_;
        BS_END _BS_END_;
        int8_t DBR_Zero[448];   //  空余，置零  448B
        int16_t Signature_word; //  校验位  2B
    };
    #pragma pack(pop)

/********** FAT16 FAT表项定义和目录项定义 **********/

    /**
     * @brief FAT表项
     * 
     * - 65520个数据扇区（block），共有8190个数据簇
     * - FAT条目值 MAX = 8193，MIN = 2
     * - 0表示空闲，1保留不用
     * - 最后一簇为0xFFFF
     */
    typedef int16_t FAT16_ENTRY;

    /**
     * @brief FAT表
     * 
     * - 扇区大小512B则FAT16的FAT表一个扇区有256个表项
     * - 这里每个完整的FAT表恰好占用(8190 + 2) / 256 = 32个扇区
     */
    std::vector<FAT16_ENTRY> fat_table;

    #pragma pack(push, 1)
    /**
     * @brief 目录项（FCB）
     * 
     */
    struct DIR_ENTRY
    {   
        /**
         * @brief 目录项实体数据，即目录项大小为32B
         */
        int8_t DIR_Name[11];    // 	短名称格式的文件名  11B
        int8_t DIR_Attr;    //  文件的属性标记  1B
        int8_t DIR_NTRes;   //  保留位，必须为0 1B
        int8_t DIR_CrtTimeTenth;    //  文件创建时间，单位为10ms    1B
        int16_t DIR_CrtTime;    //  文件创建时间    2B
        int16_t DIR_CrtDate;    //  文件创建日期    2B
        int16_t DIR_LstAccDate; //  文件最近访问日期    2B
        int16_t DIR_FstClusHI;  //  首簇簇号高16位  2B
        int16_t DIR_WrtTime;    //  文件修改时间    2B
        int16_t DIR_WrtDate;    //  文件修改日期    2B
        int16_t DIR_FstClusLO;  //  首簇簇号低16位  2B
        int32_t DIR_FileSize;   //  文件的大小，单位为字节  4B
    };
    #pragma pack(pop)

    /**
     * @brief 根目录表
     * - 共512个FCB，大小为 16348B
     * - 占用32个扇区
     */
    std::vector<DIR_ENTRY> root_entry_table;
/********** 其它必要私有成员变量 **********/
    std::string disk_name;  //  AT16虚拟磁盘文件名
    std::ofstream disk_out; //  避免重复创建 std::ofstream 对象
    DBR first_sector;
};

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

int main(int argc, const char * argv[])
{   
    if (argc != 2)
    {
        std::cerr << "USAGE: " << argv[0] << " <disk_image_name>\n";
        exit(EXIT_FAILURE);
    }
    FAT16DiskGenerator disk_instance(argv[1]);
    disk_instance.gen_ds_data();
    disk_instance.put();
    return 0;
}