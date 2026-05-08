#ifndef FAT16_HPP
#define FAT16_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <cstring>
#include <iterator>
#include <iostream>
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
    explicit FAT16(const std::string &disk_img = "FAT16.img");

    /**
     * @brief 析构函数，文件系统关闭时会尝试将缓冲数据持久化保存到虚拟磁盘镜像中
     *
     * - 注意：未能正确析构则FAT[1]不会重置为0xffff
     */
    ~FAT16();

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

    typedef uint16_t FAT16_ENTRY; //  FAT16 表项

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
        uint8_t DIR_Name[11];     // 	短名称格式的文件名  11B
        uint8_t DIR_Attr;         //  文件的属性标记  1B
        uint8_t DIR_NTRes;        //  保留位，必须为0 1B
        uint8_t DIR_CrtTimeTenth; //  文件创建时间，单位为10ms    1B
        uint16_t DIR_CrtTime;     //  文件创建时间    2B
        uint16_t DIR_CrtDate;     //  文件创建日期    2B
        uint16_t DIR_LstAccDate;  //  文件最近访问日期    2B
        uint16_t DIR_FstClusHI;   //  首簇簇号高16位  2B
        uint16_t DIR_WrtTime;     //  文件修改时间    2B
        uint16_t DIR_WrtDate;     //  文件修改日期    2B
        uint16_t DIR_FstClusLO;   //  首簇簇号低16位  2B
        uint32_t DIR_FileSize;    //  文件的大小，单位为字节  4B
    };
#pragma pack(pop)

    /********** ### Layer 4: 文件/目录操作（面向用户的 API） **********/

    // === 文件操作 ===
    bool create_file(const std::string &path);                               // 创建空文件
    bool delete_file(const std::string &path);                               // 删除文件
    bool read_file(const std::string &path, std::vector<char> &buffer);      // 读整个文件
    bool write_file(const std::string &path, const std::vector<char> &data); // 覆写整个文件

    // === 目录操作 ===
    bool create_dir(const std::string &path);                 // mkdir
    bool remove_dir(const std::string &path);                 // rmdir（目录必须为空）
    std::vector<DIR_ENTRY> list_dir(const std::string &path); // ls
    void show_pwd() const { std::cout << this->pwd << '\n'; } // pwd
    bool change_dir(const std::string &path);                 // cd

    // === 文件传输 ===
    bool export_file(const std::string &src_path, const std::string &dest_path);
    bool load_file(const std::string &src_path, const std::string &dest_path);
    void sync();

private:
    /********** Layer 1: 簇操作 **********/

    // === 簇分配与回收 ===
    FAT16_ENTRY alloc_cluster();                        // 从FAT表找一个空闲簇，标记为0xFFFF（文件最后一簇），返回簇号
    bool free_cluster_chain(FAT16_ENTRY start_cluster); // 释放从 start_cluster 开始的整个簇链

    // === 簇读写（基于簇号操作扇区组）===
    bool read_cluster(FAT16_ENTRY cluster_id, char *buffer);        // 读一整个簇到连续 buffer
    bool write_cluster(FAT16_ENTRY cluster_id, const char *buffer); // 写一整个簇到连续 buffer

    // === FAT 链操作 ===
    static constexpr FAT16_ENTRY INVALID_FAT16_ENTRY = 0x0001;
    FAT16_ENTRY follow_fat_chain(FAT16_ENTRY cluster_id);             // 返回 fat_table[cluster_id]
    FAT16_ENTRY get_last_cluster_in_chain(FAT16_ENTRY start_cluster); // 沿链找到最后一个簇

    /********** Layer 2: 目录项操作 **********/

    static constexpr FAT16_ENTRY ROOT_DIR_CLUSTER = 0x0000; //  约定簇号0为根目录簇号

    // === 目录项查找 ===
    // dir_cluster: 目录所在的起始簇号（根目录用 0 表示，因为根目录特殊）
    // name: "FILE.TXT"（8.3格式大写文件名）
    // entry: 输出参数
    // 返回：true 找到，false 未找到
    bool find_entry(FAT16_ENTRY dir_cluster, const std::string &name, DIR_ENTRY &entry);

    // === 目录项添加 ===
    // 在 dir_cluster 指向的目录中查找空闲 FCB（0xE5 或 0x00），填入 entry
    bool add_entry(FAT16_ENTRY dir_cluster, const DIR_ENTRY &entry);

    // === 目录项删除 ===
    // 标记 DIR_Name[0] = 0xE5
    // 只允许删除空目录
    bool remove_entry(FAT16_ENTRY dir_cluster, const std::string &name);

    // === 遍历目录 ===
    // 返回目录中所有非空闲项
    std::vector<DIR_ENTRY> list_entries(FAT16_ENTRY dir_cluster);

    /********** Layer 3: 路径解析 **********/

    // 删除多余 '/' 并强制转大写
    std::string assist_normalize(const std::string &path) const;

    // 检查文件名的正确性
    bool validation_name(const std::string &name) const;

    /**
     * @brief
     * 路径解析结果
     */
    struct PATH_RESULT
    {
        FAT16_ENTRY parent_dir_cluster; // 父目录的簇号（根目录为 0x0000 特殊标记）
        std::string parent_filename;    // 文件名的 8.3 格式的最后一级名称
        DIR_ENTRY entry;                // 找到的目录项（如果存在）
        bool exists;                    // 目录项是否存在
    };

    /**
     * @brief
     * 对路径进行规范化，包括：检查路径格式正确，删除多余 '/'
     * @param path 原始路径
     * @param normalized_path 规范化路径
     * @return true 可规范化
     * @return false 不可规范化
     */
    bool path_normalizer(const std::string &path, std::string &normalized_path) const;

    /**
     * @brief
     * 对完整规范路径进行简化
     * @param normalized_path 完整规范化路径
     * @param simplified_path 返回的简化路径
     * @return true 可简化
     * @return false 不可简化
     */
    bool path_simplify(const std::string &normalized_path, std::string &simplified_path) const;

    /**
     * @brief 解析路径
     *
     * 行为说明：
     * - "/" → 根目录，parent_dir_cluster = 0x0000, file_name = ""
     * - "/DIR1/FILE.TXT" → 逐级解析：先找 DIR1（子目录），再在 DIR1 中找 FILE.TXT
     * - 中间任何一级不存在 → 返回 false，entry 无效
     * - 最后一级不存在但之前都存在 → exists = false，但 parent_dir_cluster 正确
     *
     * @param path 路径字符串，路径必须为完整（不一定是绝对路径）除了根目录 "/" 以外其它路径若最后有 "/" 会被忽略，交上层语义层（Layer 4）判断
     * @param result 输出参数
     * @return true 路径解析成功（至少父目录存在）
     * @return false 路径格式错误或中间目录不存在
     */
    bool resolve_path(const std::string &path, PATH_RESULT &result);

    /********** 辅助函数 **********/

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
    bool LBA_to_PA(uint32_t cluster_id, uint32_t &block_id) const;

    /**
     * @brief
     * 检查镜像有效性
     */
    void validation_fat16(const std::string &disk_img);

    // 将8.3格式短文件名的目录项元数据转换为 std::string 对象的8.3格式短文件名
    void short_name_to_string(const uint8_t *DIR_Name, std::string &name);

    // 将目录项 DIR_Name 字段转为名称（<=8）和拓展名（<=3）
    void get_splited_dir_name(const std::string &normalized_path, std::string &_name, std::string &_ext) const;

    /********** FAT16文件系统数据成员 **********/
    std::string disk_name;
    DBR DBR_512;                               // 第一个扇区，包含 FAT16 虚拟磁盘元数据
    std::vector<FAT16_ENTRY> fat_table;        // FAT表
    FAT16_ENTRY max_cluster_id;                // 最大簇号
    std::vector<FAT16_ENTRY> free_cluster_ids; // 空闲簇号表
    std::vector<DIR_ENTRY> root_entry_table;   // 根目录表
    std::string pwd;                           // 当前目录，总以 '/' 结尾
    std::vector<DIR_ENTRY> sub_dir_file;       // 子目录，按簇加载使用,因为子目录也是“普通文件”按簇链管理，只是为了不重复创建这一对象的临时缓冲
    std::unique_ptr<BlockDevice> device;       // 抽象块设备，虚拟磁盘的底层操作封装，注意块设备中一块在这里认为是一个扇区！
};

#endif