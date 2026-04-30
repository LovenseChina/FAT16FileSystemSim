#ifndef FAT16_HPP
#define FAT16_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

class BlockDevice;  // 其他文件中实现的块设备

/**
 * @brief 
 * 
 * FAT16（小端序）核心功能模拟程序
 * inspired by YatSunOS v2 Tutorial
 * 
 * @details
 * 
 * - 一个数据块为一个扇区，大小为512B
 * - 每簇8扇区
 * - 一个分区就是一个卷（不支持多卷管理）
 * - 虚拟磁盘约32MB大小，含文件系统数据结构在内共65520个扇区
 * - 
 * @cite https://ysos.gzti.me/
 * @author Tang Jung-Chi
 */

class FAT16
{
public:
/********** FAT_ENTRY取值范围以及FAT[0]和FAT[1]常量 **********/
    static constexpr int16_t MIN = 2;
    static constexpr int16_t MAX = 8191;
    static constexpr int16_t FAT_0 = 0xFFF0;
    static constexpr int16_t FAT_1 = 0xFFFF;    // 表示正常卸载的FAT16磁盘

/********** BPB填充用常量 **********/

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

/********** BS_END填充用常量 **********/

    static constexpr int8_t BS_DRV_NUM = 0; //  0x13中断，无关字段取0即可
    static constexpr int8_t BS_RESERVED_1 = 0;
    static constexpr int8_t BS_BOOT_SIG = 0x29;    //  检验启动扇区的完整性的签名，提供了 BS_VolID、BS_VolLab 等扩展字段，则 BS_BootSig 必须为 0x29
    static constexpr int32_t BS_VOl_ID = 0; //  卷的序列号，可忽略

/********** DBR签名填充用常量 **********/
    static constexpr int16_t SIGNATURE_WORD = 0xAA55;

    /**
     * @brief 构造函数，打开或创建块设备
     * @param disk_img 磁盘镜像文件路径
     */
     explicit FAT16(const std::string & disk_img = "FAT16.disk");

     /**
      * @brief 析构函数，文件系统关闭时会尝试将缓冲数据持久化保存到虚拟磁盘镜像中
      */
    ~FAT16();

    /**
     * @brief 格式化一个磁盘镜像文件
     * 
     * 尝试清空磁盘镜像文件内所有数据
     * 
     * @return true 成功
     * @return false 失败
     */
    bool format();

    /**
     * @brief 列出当前目录下所有文件
     */
    void ls() const;

    /**
     * @brief 创建空文件
     * 
     * @param file_name 文件名
     * @return true 成功
     * @return false 失败（同名文件或FCB不足）
     */
    bool touch(const std::string & file_name);

    /**
     * @brief 向文件写入内容（覆盖模式）
     * 
     * @param file_name 文件名
     * @param content 内容
     * @return true 成功
     * @return false 失败（文件不存在或虚拟磁盘容量不足等）
     */
    bool write_file(const std::string & file_name, const std::string & content);

    /**
     * @brief 显示文件内容
     * 
     * @param file_name 文件名
     * @return true 成功
     * @return false 失败（文件不存在等）
     */
    bool cat(const std::string & file_name) const;

    /**
     * @brief 删除单个文件
     * 
     * @param file_name 文件名 
     * @return true 成功
     * @return false 失败（文件不存在等）
     */
    bool rm(const std::string & file_name);

    /**
     * @brief 进入指定文件夹
     * 
     * @param dir_path 目录路径 
     * @return true 成功
     * @return false 失败（路径不存在）
     */
    bool cd(const std::string & dir_path);

    /**
     * @brief 检查文件是否存在
     * 
     * @param file_name 文件名
     * @return true 文件存在
     * @return false 文件不存在
     */
    bool exist(const std::string &file_name) const;

    /**
     * @brief 手动同步数据到虚拟磁盘
     * 
     */
    void sync();

private:
/********** FAT16必要数据结构 **********/

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
     * - 用指针指向FAT表，采用堆分配
     * - 作为私有成员变量
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
     * - 思路类似FAT表的私有动态成员
     */
    std::vector<DIR_ENTRY> root_entry_table;

/********** FAT16低级操作 **********/

    /**
     * @brief 按簇号读出一个簇
     * 
     * @param fat_entry FAT表项
     * @param dest_buffer 目的数据缓冲，大小至少为8 * 512B（一般取8 * 512B） 
     * @return true 成功
     * @return false 失败
     */
    bool read_cluster(FAT16_ENTRY & fat_entry, int8_t * dest_buffer) const;

    /**
     * @brief 按簇号写入一个簇
     * 
     * @param fat_entry FAT表项
     * @param src_buffer 源数据缓冲，大小至少为8 * 512B（一般取8 * 512B）
     * @return true 成功
     * @return false 失败
     */
    bool write_cluster(FAT16_ENTRY & fat_entry, const int8_t * src_buffer);

    /**
     * @brief 读取FAT表项
     * 
     * @param fat_entry 当前FAT表项
     * @return const FAT16_ENTRY 下一个FAT表项
     */
    const FAT16_ENTRY get_next_entry(const FAT16_ENTRY & fat_entry) const;

    /**
     * @brief 获取一个空簇
     * 
     * @return const FAT16_ENTRY 空簇的簇号（FAT表项）
     */
    const FAT16_ENTRY get_empty_cluster() const;

    /**
     * @brief 检查是否为格式化的FAT16虚拟磁盘
     * 
     * @param first_sector 第一个扇区
     * @return true 已格式化
     * @return false 未格式化
     */
    bool formated(const int8_t * first_sector);

/********** FAT16文件系统其他数据成员 **********/
    std::unique_ptr<BlockDevice> device;
    bool is_formated;
    /**
     * @brief 当前文件夹的目录项
     */
    std::vector<DIR_ENTRY> pwd;
};

#endif