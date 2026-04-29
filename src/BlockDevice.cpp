#include "BlockDevice.hpp"

/*********** 抽象基类BlockDevice实现 ***********/

BlockDevice::BlockDevice(size_t block_size, size_t total_blocks) :
    block_size_(block_size),
    total_blocks_(total_blocks),
    data_(block_size * total_blocks, 0)    {}

bool BlockDevice::read_block(uint32_t block_id, char *buffer) const
{   
    if (block_id >= this->total_blocks_ || block_id < 0)
    {   
        std::cerr << "Invalid block_id: " << block_id << std::endl;
        return false;
    }
    else
    {
        const char *base = this->data_.data();
        memcpy(buffer, base + block_id * this->block_size_, this->block_size_);
        return true;
    }
}

bool BlockDevice::write_block(uint32_t block_id, const char *buffer)
{
    if (block_id >= this->total_blocks_ || block_id < 0)
    {
        std::cerr << "Invalid block_id: " << block_id << std::endl;
        return false;
    }
    else 
    {
        char *base = this->data_.data();
        memcpy(base + block_id * this->block_size_, buffer,this->block_size_);
        return true;
    }
}

/*********** 派生类FileBackedBlockDevice实现 ***********/

FileBackedBlockDevice::FileBackedBlockDevice(const std::string &filename, size_t block_size, size_t total_blocks) :
    filename_(filename),    
    BlockDevice(block_size, total_blocks)
{
    std::ifstream fin(filename.c_str(), std::ios_base::binary | std::ios_base::ate);
    if (!fin.good())
    {
        std::cerr << "\"" << filename << "\" is not usable. Creating new \"" << filename << "\"\n";
        this->create_empty_file();
    }
    else
    {
        this->load_from_file();
    }
}

FileBackedBlockDevice::~FileBackedBlockDevice()
{
    this->flush_to_file();
}

void FileBackedBlockDevice::flush_to_file() 
{    
    std::ofstream fout(this->filename_.c_str(), std::ios_base::binary);
    if (!fout.good())
    {
        std::cerr << "Error in opening \"" << this->filename_ << "\"!\n";
        exit(EXIT_FAILURE);
    }
    fout.write(this->data_.data(), this->block_size_ * this->total_blocks_);
    if (!fout.good())
    {
        std::cerr << "Cannot write to \"" << this->filename_ << "\"!\n";
    }
    fout.close();
}

void FileBackedBlockDevice::load_from_file()
{
    std::ifstream fin(this->filename_.c_str(), std::ios_base::binary);
    if (!fin.good())
    {
        std::cerr << "Error in opening \"" << this->filename_ << "\"!\n";
        exit(EXIT_FAILURE);
    }

    fin.seekg(0, std::ios::end);
    size_t file_size = static_cast<size_t>(fin.tellg());
    fin.seekg(0, std::ios::beg);
    size_t expected_size = this->block_size_ * this->total_blocks_;
    if (expected_size != file_size) {
        std::cerr << "Warning: disk file size mismatch. Expected " << expected_size
                  << " bytes, got " << file_size << ". Reinitializing." << std::endl;
        this->data_.assign(expected_size, 0);
        this->create_empty_file();
        return;
    }

    fin.read(this->data_.data(), this->block_size_ * this->total_blocks_);
    if (!fin.good())
    {
        std::cerr << "Cannot read from \"" << this->filename_ << "\"!\n";
    }
    fin.close();
}

void FileBackedBlockDevice::create_empty_file()
{
    std::fill(this->data_.begin(), this->data_.end(), 0);
    this->flush_to_file();
}