#ifndef BLOCK_DEVICE_HPP
#define BLOCK_DEVICE_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>

/**
 * @brief 块设备抽象基类
 *
 * 模拟块设备（硬盘）的基本操作：读取/写入固定大小的块。
 * 支持两种实现：内存模拟（用于演示） 或 文件持久化。
 */
class BlockDevice
{
public:
    /**
     * @brief 构造函数
     * @param block_size 块大小（字节）
     * @param total_blocks 总块数
     */
    BlockDevice(size_t block_size, size_t total_blocks);

    BlockDevice(const BlockDevice &) = delete;
    BlockDevice &operator=(const BlockDevice &) = delete;
    virtual ~BlockDevice() = default;

    /**
     * @brief 读取一个块
     * @param block_id 块编号（从0开始）
     * @param buffer 输出缓冲区，大小至少为block_size_
     * @return true成功，false失败（块号越界等）
     */
    bool read_block(uint32_t block_id, char *buffer) const;

    /**
     * @brief 写入一个块
     * @param block_id 块编号
     * @param buffer 输入缓冲区，大小至少为block_size_
     * @return true成功，false失败
     */
    bool write_block(uint32_t block_id, const char *buffer);

    /**
     * @brief 获取总块数
     */
    size_t total_blocks() const { return total_blocks_; }

    /**
     * @brief 获取块大小
     */
    size_t block_size() const { return block_size_; }

    /**
     * @brief 持久化到文件（如果实现是文件支持的，可调用；内存实现可忽略）
     */
    virtual void flush_to_file() {}

protected:
    size_t block_size_;
    size_t total_blocks_;
    std::vector<char> data_; // 内存模拟磁盘
};

/**
 * @brief 基于文件的块设备（持久化）
 *
 * 将磁盘数据保存在外部文件中，程序重启后可重新加载。
 */
class FileBackedBlockDevice : public BlockDevice
{
public:
    /**
     * @brief 构造文件块设备
     * @param filename 磁盘镜像文件路径
     * @param block_size 块大小
     * @param total_blocks 总块数（如果文件不存在则按此创建；存在则自动适配）
     */
    FileBackedBlockDevice(const std::string &filename,
                          size_t block_size,
                          size_t total_blocks);

    ~FileBackedBlockDevice();

    /**
     * @brief 将内存中的数据同步到磁盘文件
     */
    void flush_to_file() override;

private:
    std::string filename_;
    void load_from_file();    // 从文件加载数据
    void create_empty_file(); // 创建空磁盘文件
};

#endif