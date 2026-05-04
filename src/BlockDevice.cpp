#include "../include/BlockDevice.hpp"

/**
 * @file BlockDevice.cpp
 * @author Tang Jung-Chi
 * @version 0.1
 * @date 2026-05-04
 */

/*********** 抽象基类BlockDevice实现 ***********/

BlockDevice::BlockDevice(uint32_t block_size, uint32_t total_blocks) : block_size(block_size),
                                                                       total_blocks(total_blocks)
{
}

/*********** 派生类FileBackedBlockDevice实现 ***********/

FileBackedBlockDevice::FileBackedBlockDevice(const std::string &filename, uint32_t block_size, uint32_t total_blocks) : BlockDevice(block_size, total_blocks),
                                                                                                                        filename(filename), cache(FileBackedBlockDevice::CACHE_SIZE)
{
    std::for_each(this->cache.begin(), this->cache.end(), [block_size](CacheItem &ci)
                  { ci.data_block.resize(block_size); }); //  预分配 cache 的数据域大小
}

FileBackedBlockDevice::~FileBackedBlockDevice()
{
    this->flush_to_file();
}

bool FileBackedBlockDevice::read_block(uint32_t block_id, char *buffer)
{
    if (block_id >= this->total_blocks)
    {
        std::cerr << "Invalid block_id: " << block_id << ".\n";
        return false;
    }
    else
    {
        if (!this->read_cache(block_id, buffer))
        {
            std::cerr << "Cache error: Cannot read.\nAbort.\n";
            exit(EXIT_FAILURE);
        }
        return true;
    }
}

bool FileBackedBlockDevice::write_block(uint32_t block_id, const char *buffer)
{
    if (block_id >= this->total_blocks)
    {
        std::cerr << "Invalid block_id: " << block_id << ".\n";
        return false;
    }
    else
    {
        if (!this->write_cache(block_id, buffer))
        {
            std::cerr << "Cache error: Cannot write.\nAbort.\n";
            exit(EXIT_FAILURE);
        }
        return true;
    }
}

void FileBackedBlockDevice::flush_to_file()
{
    std::fstream fout(this->filename, std::ios_base::binary | std::ios_base::out | std::ios_base::in); //  二进制下不截断写文件
    if (!fout.is_open())
    {
        std::cerr << "Virtual disk file \"" << this->filename << "\" cannot open.\nAbort.\n";
        exit(EXIT_FAILURE);
    }
    for (std::vector<CacheItem>::iterator it = this->cache.begin(); it != this->cache.end(); ++it) // 对每行 cache 进行检查
    {
        if (it->valid && it->dirty) // 若为有效脏数据则写回文件
        {
            fout.seekp(it->block_id * this->block_size, std::ios::beg); // 绝对地址计算：块号 * 块大小
            fout.write(it->data_block.data(), this->block_size);
        }
    }
    fout.close();
}

uint32_t FileBackedBlockDevice::block_id_to_index(uint32_t block_id) const
{
    return block_id % FileBackedBlockDevice::CACHE_SIZE;
}

bool FileBackedBlockDevice::write_cache(uint32_t block_id, const char *buffer)
{
    uint32_t index = this->block_id_to_index(block_id);
    if (!(this->cache[index].valid && this->cache[index].block_id == block_id)) // 写缺失操作
    {
        if (this->cache[index].valid && this->cache[index].dirty) //  驱逐旧 cache 行的写回策略
        {
            std::fstream fout(this->filename, std::ios_base::binary | std::ios_base::out | std::ios_base::in); //  二进制下不截断写文件
            if (!fout.is_open())
            {
                return false;
            }
            fout.seekp(this->cache[index].block_id * this->block_size, std::ios::beg); // 绝对地址计算：块号 * 块大小
            fout.write(this->cache[index].data_block.data(), this->block_size);
            fout.close();
        }
    }
    //  采用写不分配策略，直接写入 cache 即可
    memcpy(this->cache[index].data_block.data(), buffer, this->block_size);
    this->cache[index].dirty = true;
    this->cache[index].valid = true;
    this->cache[index].block_id = block_id;
    return true;
}

bool FileBackedBlockDevice::read_cache(uint32_t block_id, char *buffer)
{
    uint32_t index = this->block_id_to_index(block_id);
    if (!(this->cache[index].valid && this->cache[index].block_id == block_id)) // 读缺失操作
    {
        if (this->cache[index].valid && this->cache[index].dirty) //  驱逐旧 cache 行的写回策略
        {
            std::fstream fout(this->filename, std::ios_base::binary | std::ios_base::out | std::ios_base::in); //  二进制下不截断写文件
            if (!fout.is_open())
            {
                return false;
            }
            fout.seekp(this->cache[index].block_id * this->block_size, std::ios::beg); // 绝对地址计算：块号 * 块大小
            fout.write(this->cache[index].data_block.data(), this->block_size);
            fout.close();
        }
        //  读取数据时先调块到 cache
        std::ifstream fin(this->filename, std::ios_base::binary);
        if (!fin.is_open())
        {
            return false;
        }
        fin.seekg(block_id * this->block_size, std::ios::beg);
        fin.read(this->cache[index].data_block.data(), this->block_size);
        this->cache[index].dirty = false;
        this->cache[index].valid = true;
        this->cache[index].block_id = block_id;
        fin.close();
    }
    //  读取数据
    memcpy(buffer, this->cache[index].data_block.data(), this->block_size);
    return true;
}

FileBackedBlockDevice::CacheItem::CacheItem() : dirty(false), valid(false), data_block(), block_id(0) {}