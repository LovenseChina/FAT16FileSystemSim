#ifndef BLOCK_DEVICE_HPP
#define BLOCK_DEVICE_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <iterator>
#include <algorithm>

/**
 * @brief 块设备抽象基类
 * - 仅提供统一操作界面
 */
class BlockDevice
{
public:
    /**
     * @brief 构造函数
     * @param block_size 块大小（字节）
     * @param total_blocks 总块数
     */
    BlockDevice(uint32_t block_size, uint32_t total_blocks);

    BlockDevice(const BlockDevice &) = delete;
    BlockDevice &operator=(const BlockDevice &) = delete;

    virtual ~BlockDevice() = default;

    /**
     * @brief 读取一个块
     * @param block_id 块编号（从0开始）
     * @param buffer 输出缓冲区，大小至少为block_size
     * @return true成功，false失败（块号越界等）
     */
    virtual bool read_block(uint32_t block_id, char * buffer) = 0;

    /**
     * @brief 写入一个块
     * @param block_id 块编号
     * @param buffer 输入缓冲区，大小至少为block_size
     * @return true成功，false失败
     */
    virtual bool write_block(uint32_t block_id, const char * buffer) = 0;

    /**
     * @brief 将所有脏数据强制同步到文件中
     */
    virtual void flush_to_file() = 0;
protected:
    size_t block_size;
    size_t total_blocks;
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
                          uint32_t block_size,
                          uint32_t total_blocks);

    /**
     * @brief 析构函数
     * 
     * - 关键作用是调用函数将内存脏数据同步到磁盘文件
     */
    ~FileBackedBlockDevice();
    
    /**
     * @brief 读取一个块
     * 
     * @param block_id 块号（所谓逻辑地址LBA）
     * @param buffer 内存缓冲，大小至少为块大小
     * @return true 读成功
     * @return false 读失败
     */
    virtual bool read_block(uint32_t block_id, char * buffer);
    
    /**
     * @brief 写入一个块
     * 
     * @param block_id 块号（所谓逻辑地址LBA）
     * @param buffer 内存缓冲，大小至少为块大小
     * @return true 写成功
     * @return false 写失败
     */
    virtual bool write_block(uint32_t block_id, const char * buffer);
    
    /**
     * @brief 将所有 脏 数据强制同步到文件中
     */
    virtual void flush_to_file();
private:
/************ 直接映射缓存相关操作 ************/

    inline uint32_t block_id_to_index(uint32_t block_id) const;
    bool write_cache(uint32_t block_id, const char * buffer);
    bool read_cache(uint32_t block_id, char * buffer);

/************ 私有成员和静态常量 ************/

    std::string filename;
    struct CacheItem
    {
        bool dirty;
        bool valid;
        std::vector<char> data_block;
        uint32_t block_id;
        CacheItem();
    };
    static constexpr int CACHE_SIZE = 128;
    std::vector<CacheItem> cache;
};

#endif