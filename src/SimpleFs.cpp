#include "../include/SimpleFs.hpp"
#include "../include/BlockDevice.hpp"
#include <cstring>
#include <iostream>
#include <algorithm>
#include <sstream>

constexpr size_t SimpleFS::BLOCK_SIZE;
constexpr size_t SimpleFS::TOTAL_BLOCKS;
constexpr size_t SimpleFS::INODE_COUNT;
constexpr uint32_t SimpleFS::INODE_TABLE_START_BLOCK;
constexpr uint32_t SimpleFS::DATA_BLOCK_START;
constexpr size_t SimpleFS::DIRECT_BLOCKS_PER_INODE;

// ==================== 公有接口 ====================

SimpleFS::SimpleFS(const std::string &disk_image)
    : is_formatted_(false)
{
    // 创建块设备（可根据磁盘镜像路径决定使用文件型还是纯粹内存型）
    // 这里统一使用文件型，如果disk_image为空则使用内存型（但为简单，总是使用文件）
    dev_.reset(new FileBackedBlockDevice(disk_image, SimpleFS::BLOCK_SIZE, TOTAL_BLOCKS));

    // 检查是否已经格式化：尝试加载超级块或根目录inode
    // 简单做法：读根目录inode（inode0），若其in_use标志为真且size有效则认为已格式化
    // 由于inode未持久化in_use字段，我们从根目录是否存在来确定
    // 设计：inode0若size>0或占用了数据块，则认为已格式化
    Inode root;
    if (read_inode(0, root) && root.in_use)
    {
        is_formatted_ = true;
    }
    else
    {
        // 未格式化，需要显式调用format
        std::cout << "Filesystem not formatted. Use 'format' command." << std::endl;
    }

    // 重建空闲块位图
    rebuild_free_blocks_bitmap();
}

SimpleFS::~SimpleFS()
{
    if (dev_)
    {
        dev_->flush_to_file();
    }
}

bool SimpleFS::format()
{
    // 1. 清空所有数据块（实际上只需清空inode表和数据区）
    // 使用块设备写入全零
    char zero_block[SimpleFS::BLOCK_SIZE];
    std::memset(zero_block, 0, SimpleFS::BLOCK_SIZE);
    for (uint32_t i = 0; i < TOTAL_BLOCKS; ++i)
    {
        dev_->write_block(i, zero_block);
    }

    // 2. 初始化inode表（全部清零，并设置in_use false）
    Inode empty_inode;
    empty_inode.in_use = false;
    for (uint32_t i = 0; i < INODE_COUNT; ++i)
    {
        write_inode(i, empty_inode);
    }

    // 3. 创建根目录inode（inode 0）
    Inode root_inode;
    root_inode.in_use = true;
    root_inode.size = 0;
    for (size_t i = 0; i < DIRECT_BLOCKS_PER_INODE; ++i)
    {
        root_inode.blocks[i] = 0;
    }
    write_inode(0, root_inode);

    // 4. 更新空闲块位图
    rebuild_free_blocks_bitmap();

    // 5. 标记为已格式化
    is_formatted_ = true;

    // 同步到磁盘
    dev_->flush_to_file();
    std::cout << "Format complete. Filesystem ready." << std::endl;
    return true;
}

void SimpleFS::ls() const
{
    if (!is_formatted_)
    {
        std::cerr << "Error: filesystem not formatted. Please run 'format' first." << std::endl;
        return;
    }

    std::vector<DirEntry> entries = read_root_directory();
    if (entries.empty())
    {
        std::cout << "(empty directory)" << std::endl;
        return;
    }

    std::cout << "Files in root directory:" << std::endl;
    for (const auto &entry : entries)
    {
        Inode inode;
        if (read_inode(entry.inode, inode))
        {
            std::cout << "  " << entry.name
                      << " (inode=" << entry.inode
                      << ", size=" << inode.size << " bytes)" << std::endl;
        }
        else
        {
            std::cout << "  " << entry.name << " (inode=" << entry.inode << ", ???)" << std::endl;
        }
    }
}

bool SimpleFS::touch(const std::string &name)
{
    if (!is_formatted_)
    {
        std::cerr << "Error: filesystem not formatted. Please run 'format' first." << std::endl;
        return false;
    }

    // 检查文件名合法性
    if (name.empty() || name.length() > 255)
    {
        std::cerr << "Error: invalid file name length." << std::endl;
        return false;
    }

    // 检查是否已存在
    if (lookup(name) != 0)
    {
        std::cerr << "Error: file '" << name << "' already exists." << std::endl;
        return false;
    }

    // 分配inode
    uint32_t inode_num = alloc_inode();
    if (inode_num == 0)
    {
        std::cerr << "Error: no free inode available." << std::endl;
        return false;
    }

    // 创建空inode
    Inode new_inode;
    new_inode.in_use = true;
    new_inode.size = 0;
    for (int i = 0; i < static_cast<int>(DIRECT_BLOCKS_PER_INODE); ++i)
        new_inode.blocks[i] = 0;
    if (!write_inode(inode_num, new_inode))
    {
        free_inode(inode_num);
        return false;
    }

    // 添加目录项
    if (!add_dir_entry(name, inode_num))
    {
        free_inode(inode_num);
        return false;
    }

    std::cout << "File '" << name << "' created (inode " << inode_num << ")." << std::endl;
    return true;
}

bool SimpleFS::write_file(const std::string &name, const std::string &content)
{
    if (!is_formatted_)
    {
        std::cerr << "Error: filesystem not formatted. Please run 'format' first." << std::endl;
        return false;
    }

    uint32_t inode_num = lookup(name);
    if (inode_num == 0)
    {
        std::cerr << "Error: file '" << name << "' not found. Use 'touch' first." << std::endl;
        return false;
    }

    // 读取原有inode
    Inode inode;
    if (!read_inode(inode_num, inode))
    {
        return false;
    }

    // 释放原有数据块
    for (size_t i = 0; i < DIRECT_BLOCKS_PER_INODE; ++i)
    {
        if (inode.blocks[i] != 0)
        {
            free_block(inode.blocks[i]);
            inode.blocks[i] = 0;
        }
    }

    // 计算需要多少块
    size_t data_len = content.size();
    size_t needed_blocks = (data_len + SimpleFS::BLOCK_SIZE - 1) / SimpleFS::BLOCK_SIZE;
    if (needed_blocks > DIRECT_BLOCKS_PER_INODE)
    {
        std::cerr << "Error: file too large (max "
                  << (DIRECT_BLOCKS_PER_INODE * SimpleFS::BLOCK_SIZE) << " bytes)." << std::endl;
        // 恢复原状（似乎没有恢复必要，因为已经释放了）
        return false;
    }

    // 分配新块并写入数据
    std::vector<uint32_t> new_blocks;
    for (size_t i = 0; i < needed_blocks; ++i)
    {
        uint32_t block_id = alloc_block();
        if (block_id == 0)
        {
            // 分配失败，回滚已分配的块
            std::cerr << "Error: out of free blocks." << std::endl;
            for (uint32_t b : new_blocks)
                free_block(b);
            return false;
        }
        new_blocks.push_back(block_id);
        size_t offset = i * SimpleFS::BLOCK_SIZE;
        size_t to_copy = std::min(SimpleFS::BLOCK_SIZE, data_len - offset);
        char buffer[SimpleFS::BLOCK_SIZE];
        std::memset(buffer, 0, SimpleFS::BLOCK_SIZE);
        std::memcpy(buffer, content.c_str() + offset, to_copy);
        dev_->write_block(block_id, buffer);
    }

    // 更新inode
    for (size_t i = 0; i < new_blocks.size(); ++i)
    {
        inode.blocks[i] = new_blocks[i];
    }
    inode.size = static_cast<uint32_t>(data_len);
    if (!write_inode(inode_num, inode))
    {
        // 回滚
        for (uint32_t b : new_blocks)
            free_block(b);
        return false;
    }

    std::cout << "Wrote " << data_len << " bytes to '" << name << "'." << std::endl;
    return true;
}

bool SimpleFS::cat(const std::string &name) const
{
    if (!is_formatted_)
    {
        std::cerr << "Error: filesystem not formatted. Please run 'format' first." << std::endl;
        return false;
    }

    uint32_t inode_num = lookup(name);
    if (inode_num == 0)
    {
        std::cerr << "Error: file '" << name << "' not found." << std::endl;
        return false;
    }

    Inode inode;
    if (!read_inode(inode_num, inode))
    {
        return false;
    }

    if (inode.size == 0)
    {
        std::cout << "(empty file)" << std::endl;
        return true;
    }

    // 读取数据块并拼接
    std::string content;
    for (size_t i = 0; i < DIRECT_BLOCKS_PER_INODE && inode.blocks[i] != 0; ++i)
    {
        char buffer[SimpleFS::BLOCK_SIZE];
        if (!dev_->read_block(inode.blocks[i], buffer))
        {
            std::cerr << "Error: failed to read block " << inode.blocks[i] << std::endl;
            return false;
        }
        size_t to_read = std::min(static_cast<size_t>(SimpleFS::BLOCK_SIZE),
                                  static_cast<size_t>(inode.size) - content.size());
        content.append(buffer, to_read);
    }

    std::cout << content << std::endl;
    return true;
}

bool SimpleFS::rm(const std::string &name)
{
    if (!is_formatted_)
    {
        std::cerr << "Error: filesystem not formatted. Please run 'format' first." << std::endl;
        return false;
    }

    uint32_t inode_num = lookup(name);
    if (inode_num == 0)
    {
        std::cerr << "Error: file '" << name << "' not found." << std::endl;
        return false;
    }

    // 删除目录项
    if (!remove_dir_entry(name))
    {
        return false;
    }

    // 释放inode（会自动释放其数据块）
    free_inode(inode_num);

    std::cout << "File '" << name << "' deleted." << std::endl;
    return true;
}

bool SimpleFS::exists(const std::string &name) const
{
    if (!is_formatted_)
        return false;
    return lookup(name) != 0;
}

void SimpleFS::sync()
{
    dev_->flush_to_file();
}

// ==================== 私有辅助函数 ====================

bool SimpleFS::read_inode(uint32_t inode_num, Inode &out) const
{
    if (inode_num >= INODE_COUNT)
        return false;

    // 计算inode所在的块和偏移
    // 假设inode表从INODE_TABLE_START_BLOCK开始，每个块可以存放 SimpleFS::BLOCK_SIZE / sizeof(Inode) 个inode
    // 但我们的Inode结构是内存表示（包含bool），不适合直接作为磁盘布局。需要序列化。
    // 为了简化，我们将inode以固定的32字节结构存储（与内存中的不同），避免bool对齐问题。
    // 这里为了降低复杂度，我们直接使用内存表示并使用memcpy从块中读取（假设块是干净的）。
    // 真实产品中应该定义磁盘inode格式。本模拟器为简捷，我们假设BlockDevice直接存储Inode对象数组。
    // 更简单：将inode表存储在单独的区域，每个inode占用固定大小（如32字节），使用read/write序列化。
    // 重构：定义磁盘inode结构体（POD），然后转换。
    struct DiskInode
    {
        uint32_t size;
        uint32_t blocks[DIRECT_BLOCKS_PER_INODE];
        uint8_t in_use; // 1字节
        uint8_t padding[3];
    };
    static_assert(sizeof(DiskInode) == (4 + 4 * 12 + 4), "DiskInode size mismatch");

    uint32_t inodes_per_block = SimpleFS::BLOCK_SIZE / sizeof(DiskInode);
    uint32_t block_id = INODE_TABLE_START_BLOCK + inode_num / inodes_per_block;
    uint32_t offset_in_block = (inode_num % inodes_per_block) * sizeof(DiskInode);

    char block_data[SimpleFS::BLOCK_SIZE];
    if (!dev_->read_block(block_id, block_data))
        return false;

    DiskInode disk_inode;
    std::memcpy(&disk_inode, block_data + offset_in_block, sizeof(DiskInode));

    // 转换到内存Inode
    out.size = disk_inode.size;
    for (size_t i = 0; i < DIRECT_BLOCKS_PER_INODE; ++i)
        out.blocks[i] = disk_inode.blocks[i];
    out.in_use = (disk_inode.in_use != 0);
    return true;
}

bool SimpleFS::write_inode(uint32_t inode_num, const Inode &inode)
{
    if (inode_num >= INODE_COUNT)
        return false;

    struct DiskInode
    {
        uint32_t size;
        uint32_t blocks[DIRECT_BLOCKS_PER_INODE];
        uint8_t in_use;
        uint8_t padding[3];
    };

    DiskInode disk_inode;
    disk_inode.size = inode.size;
    for (size_t i = 0; i < DIRECT_BLOCKS_PER_INODE; ++i)
        disk_inode.blocks[i] = inode.blocks[i];
    disk_inode.in_use = inode.in_use ? 1 : 0;
    std::memset(disk_inode.padding, 0, sizeof(disk_inode.padding));

    uint32_t inodes_per_block = SimpleFS::BLOCK_SIZE / sizeof(DiskInode);
    uint32_t block_id = INODE_TABLE_START_BLOCK + inode_num / inodes_per_block;
    uint32_t offset_in_block = (inode_num % inodes_per_block) * sizeof(DiskInode);

    char block_data[SimpleFS::BLOCK_SIZE];
    if (!dev_->read_block(block_id, block_data))
        return false;
    std::memcpy(block_data + offset_in_block, &disk_inode, sizeof(DiskInode));
    return dev_->write_block(block_id, block_data);
}

uint32_t SimpleFS::alloc_inode()
{
    for (uint32_t i = 0; i < INODE_COUNT; ++i)
    {
        Inode inode;
        if (!read_inode(i, inode))
            continue;
        if (!inode.in_use)
        {
            // 找到空闲inode
            Inode new_inode;
            new_inode.in_use = true;
            new_inode.size = 0;
            for (size_t j = 0; j < DIRECT_BLOCKS_PER_INODE; ++j)
                new_inode.blocks[j] = 0;
            if (write_inode(i, new_inode))
            {
                return i;
            }
        }
    }
    return 0; // 无空闲
}

void SimpleFS::free_inode(uint32_t inode_num)
{
    Inode inode;
    if (!read_inode(inode_num, inode))
        return;
    if (!inode.in_use)
        return;

    // 释放其数据块
    for (size_t i = 0; i < DIRECT_BLOCKS_PER_INODE; ++i)
    {
        if (inode.blocks[i] != 0)
        {
            free_block(inode.blocks[i]);
        }
    }
    // 标记inode为未使用
    inode.in_use = false;
    inode.size = 0;
    write_inode(inode_num, inode);
}

uint32_t SimpleFS::alloc_block()
{
    // 使用内存中的空闲块位图加速
    // 注意：位图需要与实际块设备同步，这里我们只在内存维护，分配时同时更新位图和块内容。
    // 位图重建在格式化时和每次分配/释放后更新
    for (uint32_t i = DATA_BLOCK_START; i < TOTAL_BLOCKS; ++i)
    {
        if (i < free_blocks_.size() && !free_blocks_[i])
        {
            free_blocks_[i] = true; // 标记为已使用
            // 将块内容清零（可选，但最好清零）
            char zero[SimpleFS::BLOCK_SIZE];
            std::memset(zero, 0, SimpleFS::BLOCK_SIZE);
            dev_->write_block(i, zero);
            return i;
        }
    }
    return 0;
}

void SimpleFS::free_block(uint32_t block_id)
{
    if (block_id >= TOTAL_BLOCKS)
        return;
    if (block_id < free_blocks_.size())
    {
        free_blocks_[block_id] = false;
    }
    // 清零块内容
    char zero[SimpleFS::BLOCK_SIZE];
    std::memset(zero, 0, SimpleFS::BLOCK_SIZE);
    dev_->write_block(block_id, zero);
}

void SimpleFS::rebuild_free_blocks_bitmap()
{
    free_blocks_.assign(TOTAL_BLOCKS, false);
    // 标记元数据区域为已使用
    for (uint32_t i = 0; i < DATA_BLOCK_START; ++i)
    {
        free_blocks_[i] = true;
    }
    // 扫描inode表，找到所有已使用的数据块并标记
    for (uint32_t ino = 0; ino < INODE_COUNT; ++ino)
    {
        Inode inode;
        if (read_inode(ino, inode) && inode.in_use)
        {
            for (size_t i = 0; i < DIRECT_BLOCKS_PER_INODE; ++i)
            {
                if (inode.blocks[i] != 0 && inode.blocks[i] < TOTAL_BLOCKS)
                {
                    free_blocks_[inode.blocks[i]] = true;
                }
            }
        }
    }
    // 此外，根目录的数据块也会被inode0覆盖，没问题
}

uint32_t SimpleFS::lookup(const std::string &name) const
{
    std::vector<DirEntry> entries = read_root_directory();
    for (const auto &entry : entries)
    {
        if (name == entry.name)
        {
            return entry.inode;
        }
    }
    return 0;
}

bool SimpleFS::add_dir_entry(const std::string &name, uint32_t inode_num)
{
    // 读取根目录所有现有目录项
    std::vector<DirEntry> entries = read_root_directory();
    // 检查是否已存在
    for (const auto &e : entries)
    {
        if (e.name == name)
            return false;
    }
    // 添加新项
    DirEntry new_entry;
    new_entry.inode = inode_num;
    std::strncpy(new_entry.name, name.c_str(), 255);
    new_entry.name[255] = '\0';
    entries.push_back(new_entry);

    // 写回
    write_root_directory(entries);
    return true;
}

bool SimpleFS::remove_dir_entry(const std::string &name)
{
    std::vector<DirEntry> entries = read_root_directory();
    auto it = std::find_if(entries.begin(), entries.end(),
                           [&](const DirEntry &e)
                           { return e.name == name; });
    if (it == entries.end())
        return false;
    entries.erase(it);
    write_root_directory(entries);
    return true;
}

std::vector<SimpleFS::DirEntry> SimpleFS::read_root_directory() const
{
    std::vector<DirEntry> result;
    Inode root_inode;
    if (!read_inode(0, root_inode) || !root_inode.in_use)
    {
        return result;
    }

    // 根目录的数据块是多个连续的块，每个块存储一组DirEntry（不跨块存储）
    for (size_t i = 0; i < DIRECT_BLOCKS_PER_INODE && root_inode.blocks[i] != 0; ++i)
    {
        char block_data[SimpleFS::BLOCK_SIZE];
        if (!dev_->read_block(root_inode.blocks[i], block_data))
            continue;
        // 解析块中的目录项，每项大小为 sizeof(DirEntry) = 256+4 = 260 字节
        size_t entry_size = sizeof(DirEntry);
        for (size_t offset = 0; offset + entry_size <= SimpleFS::BLOCK_SIZE; offset += entry_size)
        {
            DirEntry entry;
            std::memcpy(&entry, block_data + offset, entry_size);
            if (entry.inode != 0)
            {
                result.push_back(entry);
            }
            else
            {
                // 遇到inode为0表示该项空闲，通常都是紧凑存储，不跳过空间，但为了简单允许跳过
                continue;
            }
        }
    }
    return result;
}

void SimpleFS::write_root_directory(const std::vector<DirEntry> &entries)
{
    Inode root_inode;
    if (!read_inode(0, root_inode) || !root_inode.in_use)
    {
        // 如果根不存在，重新创建
        root_inode.in_use = true;
        root_inode.size = 0;
        for (int i = 0; i < static_cast<int>(DIRECT_BLOCKS_PER_INODE); ++i)
            root_inode.blocks[i] = 0;
    }

    // 释放原有的目录数据块
    for (size_t i = 0; i < DIRECT_BLOCKS_PER_INODE; ++i)
    {
        if (root_inode.blocks[i] != 0)
        {
            free_block(root_inode.blocks[i]);
            root_inode.blocks[i] = 0;
        }
    }

    // 如果没有任何目录项，只清空根目录大小即可
    if (entries.empty())
    {
        root_inode.size = 0;
        write_inode(0, root_inode);
        return;
    }

    // 计算需要多少块 (每个块能容纳的目录项数)
    size_t entry_size = sizeof(DirEntry);
    size_t entries_per_block = SimpleFS::BLOCK_SIZE / entry_size;
    size_t num_blocks = (entries.size() + entries_per_block - 1) / entries_per_block;
    if (num_blocks > DIRECT_BLOCKS_PER_INODE)
    {
        std::cerr << "Error: root directory too many entries (max "
                  << DIRECT_BLOCKS_PER_INODE * entries_per_block << ")." << std::endl;
        return;
    }

    // 分配新块并写入
    for (size_t block_idx = 0; block_idx < num_blocks; ++block_idx)
    {
        uint32_t block_id = alloc_block();
        if (block_id == 0)
        {
            std::cerr << "Error: out of blocks while writing root directory." << std::endl;
            return;
        }
        root_inode.blocks[block_idx] = block_id;

        char block_data[SimpleFS::BLOCK_SIZE];
        std::memset(block_data, 0, SimpleFS::BLOCK_SIZE);
        size_t start = block_idx * entries_per_block;
        size_t end = std::min(start + entries_per_block, entries.size());
        for (size_t i = start; i < end; ++i)
        {
            size_t offset = (i - start) * entry_size;
            std::memcpy(block_data + offset, &entries[i], entry_size);
        }
        dev_->write_block(block_id, block_data);
    }

    // 更新根目录大小（可选，未使用size字段）
    root_inode.size = static_cast<uint32_t>(entries.size());
    write_inode(0, root_inode);
}

SimpleFS::Inode SimpleFS::get_root_inode() const
{
    Inode inode;
    read_inode(0, inode);
    return inode;
}