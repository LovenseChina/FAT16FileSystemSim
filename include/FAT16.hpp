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
    static constexpr size_t BLOCK_SIZE = 512;
    static constexpr size_t TOTAL_BLOCKS = 65617;   //1 + 32 + 32 + 32 + 65520
    static constexpr size_t RSVD_BLOCKS = 1;
    static constexpr size_t ROOT_ENT_NUMBER = 512;
    static constexpr size_t BLOCK_PER_CLUSTER = 8;
    static constexpr uint16_t MIN = 2;
    static constexpr uint16_t MAX = 8191;
    static constexpr uint16_t EMPTY = 0;
    static constexpr uint16_t LAST_CLUSTER = 0xFFFF;
    static constexpr size_t FAT16_ENTRY_NUMBER = 8192;

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

    /**
     * @brief 在 FAT 格式的卷的首扇区
     * 
     * - 分离出标准BPB和拓展BPB使代码更清晰
     */
    struct BPB
    {
        /**
         * @brief 
         * 
         * 标准BPB的36B数据
         */
        uint8_t StandardBPBData[36];

        /**
         * @brief 
         * 
         * 拓展BPB数据，512 - 36 = 476B
         * 
         */
        uint8_t ExtendBPBdata[476];

        /**
         * @brief 构造函数，创建BPB结构
         * 
         * 用于硬编码初始化BPB数据
         */
        BPB();
    };

    /**
     * @brief FAT表项
     * 
     * - 65520个数据扇区（block），共有8190个数据簇
     * - FAT条目值 MAX = 8193，MIN = 2
     * - 0表示空闲，1保留不用
     * - 最后一簇为0xFFFF
     */
    typedef uint16_t FAT16_ENTRY;

    /**
     * @brief FAT表
     * 
     * - 扇区大小512B则FAT16的FAT表一个扇区有256个表项
     * - 这里每个完整的FAT表恰好占用(8190 + 2) / 256 = 32个扇区
     * - 用指针指向FAT表，采用堆分配
     * - 作为私有成员变量
     */
    std::vector<FAT16_ENTRY> fat_table;

    /**
     * @brief 目录项（FCB）
     * 
     */
    struct DIR_ENTRY
    {   
        /**
         * @brief 目录项实体数据，即目录项大小为32B
         */
        int8_t FCBData[32];

        /**
         * @brief 目录项构造函数
         */
        DIR_ENTRY();
    };

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
    std::unique_ptr<int8_t> pwd;
};

#endif