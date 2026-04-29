#ifndef SIMPLE_FS_H
#define SIMPLE_FS_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

class BlockDevice;

/**
 * @brief 简单文件系统模拟器
 *
 * 设计特点：
 * - 使用inode管理文件元数据
 * - 仅支持根目录（不支持多级目录）
 * - 每个文件最多12个直接块（最大48KB，块大小4KB）
 * - 空闲块管理使用位图（内存中维护）
 */
class SimpleFS
{
public:
    // 常量定义（可配置）
    static constexpr size_t BLOCK_SIZE = 4096;             // 4KB 块大小
    static constexpr size_t TOTAL_BLOCKS = 256;            // 总共256块 => 1MB 磁盘
    static constexpr size_t INODE_COUNT = 64;              // 最多64个文件/目录
    static constexpr uint32_t INODE_TABLE_START_BLOCK = 1; // inode表起始块号（块0用作超级块）
    static constexpr uint32_t DATA_BLOCK_START = 20;       // 数据块起始块号
    static constexpr size_t DIRECT_BLOCKS_PER_INODE = 12;  // 直接块数量

    // inode 结构（内存表示，简化版）
    struct Inode
    {
        uint32_t size;                            // 文件大小（字节）
        uint32_t blocks[DIRECT_BLOCKS_PER_INODE]; // 直接块指针（块号，0表示未使用）
        bool in_use;                              // 是否已被使用

        Inode() : size(0), in_use(false)
        {
            for (size_t i = 0; i < DIRECT_BLOCKS_PER_INODE; ++i)
                blocks[i] = 0;
        }
    };

    // 目录项结构（根目录文件中）
    struct DirEntry
    {
        uint32_t inode; // 文件的inode编号
        char name[256]; // 文件名（最大255字节 + '\0'）
    };

public:
    /**
     * @brief 构造函数，打开或创建块设备
     * @param disk_image 磁盘镜像文件路径（为空则仅内存模式）
     */
    explicit SimpleFS(const std::string &disk_image = "virtual.disk");
    ~SimpleFS();

    /**
     * @brief 格式化文件系统（清空所有数据）
     * @return true成功，false失败
     */
    bool format();

    /**
     * @brief 列出根目录下的所有文件
     */
    void ls() const;

    /**
     * @brief 创建空文件
     * @param name 文件名
     * @return true成功，false失败（文件已存在或inode耗尽）
     */
    bool touch(const std::string &name);

    /**
     * @brief 向文件写入内容（覆盖模式）
     * @param name 文件名
     * @param content 内容
     * @return true成功，false失败（文件不存在等）
     */
    bool write_file(const std::string &name, const std::string &content);

    /**
     * @brief 显示文件内容
     * @param name 文件名
     * @return true成功，false失败
     */
    bool cat(const std::string &name) const;

    /**
     * @brief 删除文件
     * @param name 文件名
     * @return true成功，false失败
     */
    bool rm(const std::string &name);

    /**
     * @brief 检查文件是否存在
     */
    bool exists(const std::string &name) const;

    /**
     * @brief 手动同步数据到磁盘（如果使用文件块设备）
     */
    void sync();

private:
    // ---------- 低级操作 ----------
    /**
     * @brief 读取某个inode
     * @param inode_num inode编号（0 ~ INODE_COUNT-1）
     * @param out 输出inode数据
     * @return true成功，false失败（编号无效）
     */
    bool read_inode(uint32_t inode_num, Inode &out) const;

    /**
     * @brief 写入inode
     * @param inode_num inode编号
     * @param inode 要写入的数据
     * @return true成功，false失败
     */
    bool write_inode(uint32_t inode_num, const Inode &inode);

    /**
     * @brief 分配一个新的空闲inode
     * @return 分配的inode编号，若INODE_COUNT耗尽则返回0
     */
    uint32_t alloc_inode();

    /**
     * @brief 释放inode（清空其数据块并标记为空闲）
     * @param inode_num 要释放的inode编号
     */
    void free_inode(uint32_t inode_num);

    /**
     * @brief 分配一个空闲数据块
     * @return 块号，若没有空闲块返回0
     */
    uint32_t alloc_block();

    /**
     * @brief 释放一个数据块（清零并更新位图）
     * @param block_id 块号
     */
    void free_block(uint32_t block_id);

    /**
     * @brief 重建空闲块位图（根据数据块实际使用情况扫描）
     */
    void rebuild_free_blocks_bitmap();

    // ---------- 目录操作 ----------
    /**
     * @brief 在根目录中查找文件，返回其inode号
     * @param name 文件名
     * @return inode号，0表示不存在
     */
    uint32_t lookup(const std::string &name) const;

    /**
     * @brief 向根目录添加一个目录项
     * @param name 文件名
     * @param inode_num 对应的inode编号
     * @return true成功，false失败（目录已满等）
     */
    bool add_dir_entry(const std::string &name, uint32_t inode_num);

    /**
     * @brief 从根目录删除目录项
     * @param name 文件名
     * @return true成功，false失败（文件不存在）
     */
    bool remove_dir_entry(const std::string &name);

    /**
     * @brief 读取根目录的所有目录项到内存
     * @return 目录项列表
     */
    std::vector<DirEntry> read_root_directory() const;

    /**
     * @brief 将目录项列表写回根目录的数据块
     * @param entries 目录项列表
     */
    void write_root_directory(const std::vector<DirEntry> &entries);

    /**
     * @brief 加载根目录的inode编号（固定为0）
     */
    Inode get_root_inode() const;

private:
    std::unique_ptr<BlockDevice> dev_; // 块设备（多态）
    std::vector<bool> free_blocks_;    // 内存中的空闲块位图（加快分配）
    bool is_formatted_;                // 是否已格式化
};

#endif // SIMPLE_FS_H