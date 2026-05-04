#include "../include/BlockDevice.hpp"

/**
 * @file BlockDevice.cpp
 * @author Tang Jung-Chi
 * @version 0.2
 * @date 2026-05-04
 */

/*********** 抽象基类BlockDevice实现 ***********/

BlockDevice::BlockDevice(uint32_t block_size, uint32_t total_blocks) : block_size(block_size),
                                                                       total_blocks(total_blocks)
{
}

/*********** 派生类FileBackedBlockDevice实现 ***********/

FileBackedBlockDevice::FileBackedBlockDevice(const std::string &filename, uint32_t block_size, uint32_t total_blocks) : BlockDevice(block_size, total_blocks),
                                                                                                                        filename(filename), disk_io(filename, std::ios_base::binary | std::ios_base::in | std::ios_base::out)
{
    if (!this->disk_io.is_open())
    {
       std::cerr << "Virtual disk file \"" << this->filename << "\" cannot open.\nAbort.\n";
        exit(EXIT_FAILURE); 
    }
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
        this->disk_io.clear();
        this->disk_io.seekg(0, std::ios::beg);
        this->disk_io.seekg(block_id * this->block_size);
        this->disk_io.read(buffer, this->block_size);
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
        this->disk_io.clear();
        this->disk_io.seekp(0, std::ios::beg);
        this->disk_io.seekp(block_id * this->block_size);
        this->disk_io.write(buffer, this->block_size);
        return true;
    }
}

void FileBackedBlockDevice::flush_to_file()
{
    this->disk_io.flush();
    this->disk_io.close();
}