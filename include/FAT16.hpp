#ifndef FAT16_HPP
#define FAT16_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <cstring>
#include <iterator>
#include <iomanip>
#include <algorithm>

/**
 * @brief 块设备中低级的数据块就是FAT16中的扇区
 * 
 * - 注意：声明和定义由其它文件提供
 */
class BlockDevice;

/**
 * @brief 
 * 
 * FAT16（小端序）核心功能模拟程序
 * inspired by YatSunOS v2 Tutorial
 * 
 * @details
 * 
 * - 自动识别不含MBR的FAT16镜像文件（即 super floppy）
 * - 维护一个DBR并依此构造块设备文件
 * - 目前只包含 YatSunOS v2 Tutorial 描述的针对特定DBR字段的核心功能
 * 
 * @cite https://ysos.gzti.me/
 * @author Tang Jung-Chi
 */

class FAT16
{
public:
    /**
     * @brief 构造函数，打开块设备文件
     * @param disk_img 磁盘镜像文件路径
     */
     explicit FAT16(const std::string & disk_img = "FAT16.img");

     /**
      * @brief 析构函数，文件系统关闭时会尝试将缓冲数据持久化保存到虚拟磁盘镜像中
      * 
      * - 注意：未能正确析构则FAT[1]不会重置为0xffff
      */
    ~FAT16();

    /**
     * @brief 导出文件到宿主机
     * 
     * @param src_file_path 来自镜像的已解析源文件路径
     * @param dest_file_path 宿主机器的目标文件路径
     * @return true 导出成功
     * @return false 导出失败
     */
    bool export_file(const std::string & src_file_path, const std::string & dest_file_path);

    /**
     * @brief 加载文件到镜像
     * 
     * @param src_file_path 来自宿主机器的源文件路径
     * @param dest_file_path 镜像的已解析目标文件路径
     * @return true 导入成功
     * @return false 导入失败
     */
    bool load_file(const std::string & src_file_path, const std::string & dest_file_path);

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
     * @brief 列出指定目录下所有文件
     * 
     * @param dir_path 路径名，默认为当前文件夹（pwd）
     */
    void ls(const std::string & dir_path = ".") const;

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
        uint16_t BPB_BytsPerSec; //  每个扇区的字节数    2B
        uint8_t BPB_SecPerClus;  //  每个簇的扇区数量    1B
        uint16_t BPB_RsvdSecCnt; //  保留区域的扇区数量  2B  comment:保留扇区在FAT16中就是DBR所在扇区
        uint8_t BPB_NumFATs; //  FAT表数量   1B
        uint16_t BPB_RootEntCnt; //  根目录中的条目数    2B
        uint16_t BPB_TotSec16;   //  16位长度卷的总扇区数    2B
        uint8_t BPB_Media;   //  设备的类型  1B
        uint16_t BPB_FATSz16;    //  单个FAT表占用的扇区数   2B
        uint16_t BPB_SecPerTrk;  //  每个扇区的磁道数    2B
        uint16_t BPB_NumHeads;   //  磁头数量    2B
        uint32_t BPB_HiddSec;    //  分区前隐藏的扇区数  4B
        uint32_t BPB_TotSec32;   //  32位长度卷的总扇区数    4B
    };

    struct BS_END
    {
        uint8_t BS_DrvNum;   //  驱动器号    1B
        uint8_t BS_Reserved1;    //  保留位  1B
        uint8_t BS_BootSig;  //  检验启动扇区的完整性的签名  1B
        uint32_t BS_VolID;   //  卷的序列号  4B
        uint8_t BS_VolLab[11];   //  卷标    11B
        uint8_t BS_FilSysType[8];  //  描述文件系统类型    8B
    };
    
    struct DBR
    {
        uint8_t BS_jmpBoot[3];   //  跳转到启动代码处执行的指令  3B
        uint8_t BS_OEMName[8]; //  OEM厂商的名称 8B
        BPB _BPB_;
        BS_END _BS_END_;
        uint8_t DBR_Zero[448];   //  空余，置零  448B
        uint16_t Signature_word; //  校验位  2B
    };
    #pragma pack(pop)

    typedef uint16_t FAT16_ENTRY;   //  FAT16 表项

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
        uint8_t DIR_Name[11];    // 	短名称格式的文件名  11B
        uint8_t DIR_Attr;    //  文件的属性标记  1B
        uint8_t DIR_NTRes;   //  保留位，必须为0 1B
        uint8_t DIR_CrtTimeTenth;    //  文件创建时间，单位为10ms    1B
        uint16_t DIR_CrtTime;    //  文件创建时间    2B
        uint16_t DIR_CrtDate;    //  文件创建日期    2B
        uint16_t DIR_LstAccDate; //  文件最近访问日期    2B
        uint16_t DIR_FstClusHI;  //  首簇簇号高16位  2B
        uint16_t DIR_WrtTime;    //  文件修改时间    2B
        uint16_t DIR_WrtDate;    //  文件修改日期    2B
        uint16_t DIR_FstClusLO;  //  首簇簇号低16位  2B
        uint32_t DIR_FileSize;   //  文件的大小，单位为字节  4B
    };
    #pragma pack(pop)

/********** 字符串操作 **********/

/**
 * @brief   严格8.3格式短文件名转为人类易读的严格文件名
 * 
 * @param dir_name 目录项的 DIR_Name 字段
 * @param filename 人类易读的严格文件名，如 abc.txt
 * @return true 转换成功
 * @return false 转换失败
 */
bool short_file_name_to_string(const uint8_t * dir_name, std::string & filename);

/**
 * @brief 人类易读的严格文件名转为严格8.3格式短文件名
 * 
 * @param filename 人类易读的严格文件名，如 abc.txt
 * @param dir_name 目录项的 DIR_Name 字段
 * @return true 转换成功
 * @return false 转换失败
 */
bool string_to_short_file_name(const std::string & filename, uint8_t * dir_name);

/********** FAT16低级操作 **********/

    /**
     * @brief 依据DBR计算 FAT16 的总簇数
     * 
     * - 注意：必须在 FAT16::DBR_512 已经初始化后（即从镜像读入DBR后）执行
     * 
     * @return uint32_t 总簇数
     */
    uint32_t get_total_clusters() const;

    /**
     * @brief 逻辑块地址转物理地址
     * 
     * @param cluster_id 逻辑块地址/簇号
     * @param block_ids 一簇的实际起始扇区（块）的地址
     * @return true 转换成功，false 转换失败 
     */
    bool LBA_to_PA(uint32_t cluster_id, uint32_t & block_id) const;

/********** FAT16文件系统数据成员 **********/
    std::string disk_name;
    DBR DBR_512;   //  第一个扇区，包含 FAT16 虚拟磁盘元数据
    std::vector<FAT16_ENTRY> fat_table; //  FAT表
    std::vector<DIR_ENTRY> root_entry_table;    //  根目录表
    std::string pwd;    //  当前目录
    std::unique_ptr<BlockDevice> device;    //  抽象块设备，虚拟磁盘的底层操作封装
};

#endif