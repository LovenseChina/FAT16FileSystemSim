#include "../include/FAT16re.hpp"
#include "../include/BlockDevice.hpp"

FAT16::FAT16(const std::string &disk_img) : disk_name(disk_img)
{
    //  读取DBR
    std::ifstream disk_in(disk_img, std::ios_base::binary | std::ios_base::in);
    if (!disk_in.is_open())
    {
        std::cerr << "Cannot open disk image \"" << disk_img
                  << "\"\nAobrt.\n";
        exit(EXIT_FAILURE);
    }
    disk_in.read(reinterpret_cast<char *>(&this->DBR_512), 512); //  如果 BPB_BytsPerSec > 512 则存在此域，全部置零，故只需读入起始512字节

    //  构造FAT表
    uint32_t fat16_table_bytes = static_cast<uint32_t>(this->DBR_512._BPB_.BPB_FATSz16) * static_cast<uint32_t>(this->DBR_512._BPB_.BPB_BytsPerSec);
    this->fat_table.resize(fat16_table_bytes / sizeof(FAT16_ENTRY));
    disk_in.read(reinterpret_cast<char *>(this->fat_table.data()), fat16_table_bytes);
    disk_in.seekg(static_cast<uint32_t>(this->DBR_512._BPB_.BPB_FATSz16) * static_cast<uint32_t>(this->DBR_512._BPB_.BPB_BytsPerSec), std::ios::cur); //  忽略冗余FAT

    //  计算最大簇号
    this->max_cluster_id = this->get_total_clusters() + 1;

    //  构造空闲簇号表
    for (uint32_t i = 2; i <= max_cluster_id; ++i)
    {
        if (this->fat_table[i] = 0x0000)
        {
            this->free_cluster_ids.push_back(this->fat_table[i]);
        }
    }

    //  构造根目录表
    uint32_t root_table_bytes = 32 * static_cast<uint32_t>(this->DBR_512._BPB_.BPB_RootEntCnt);
    this->root_entry_table.resize(this->DBR_512._BPB_.BPB_RootEntCnt);
    disk_in.read(reinterpret_cast<char *>(this->root_entry_table.data()), root_table_bytes);

    //  关闭文件流
    disk_in.close();

    //  验证镜像
    this->validation_fat16(disk_img);

    std::cout << "Disk meta data all check clear.\n"
              << "FAT table constructed.\n"
              << "Root directory constructed.\n";

    //  初始当前路径为根目录 /
    this->pwd = "/";

    //  子目录文件初始化大小为1簇
    this->sub_dir_file.resize(static_cast<uint32_t>(this->DBR_512._BPB_.BPB_BytsPerSec) * static_cast<uint32_t>(this->DBR_512._BPB_.BPB_SecPerClus));

    //  依据DBR数据构造块设备
    uint32_t total_sectors = this->DBR_512._BPB_.BPB_TotSec16 > 0 ? this->DBR_512._BPB_.BPB_TotSec16 : this->DBR_512._BPB_.BPB_TotSec32;
    this->device.reset(new FileBackedBlockDevice(disk_img, this->DBR_512._BPB_.BPB_BytsPerSec, total_sectors));

    // 检测FAT[1]并置为0x0（脏位）
    if (this->fat_table[1] != 0xffff)
    {
        std::cout << "CAUTION: \"" << disk_img
                  << "\" was detected as not being properly removed last time!\n";
    }
    this->fat_table[1] = 0;

    std::cout << "\"" << disk_img << "\" has mounted.\n";
}

FAT16::~FAT16()
{
    if (this->device)
    {
        this->device->flush_to_file();
    }
    this->device.reset();

    this->fat_table[1] = 0xffff;
    std::fstream fout(this->disk_name, std::ios_base::binary | std::ios_base::out | std::ios_base::in);
    if (!fout.is_open())
    {
        std::cerr << "Fatal error: meta data cannot flush to \"" << this->disk_name << "\"\nAbort.\n";
        exit(EXIT_FAILURE);
    }
    fout.seekp(this->DBR_512._BPB_.BPB_BytsPerSec, std::ios::beg);
    fout.write(reinterpret_cast<const char *>(this->fat_table.data()), this->fat_table.size() * sizeof(FAT16_ENTRY));
    fout.write(reinterpret_cast<const char *>(this->root_entry_table.data()), this->root_entry_table.size() * sizeof(DIR_ENTRY));

    std::cout << "\"" << this->disk_name << "\" unmonted.\n";
}

/********** Layer 1: 簇操作 **********/

FAT16::FAT16_ENTRY FAT16::alloc_cluster()
{
    FAT16_ENTRY empty_cluster_id = this->free_cluster_ids.back();
    this->free_cluster_ids.pop_back();
    this->fat_table[empty_cluster_id] = 0xffff;
    return empty_cluster_id;
}

bool FAT16::free_cluster_chain(FAT16_ENTRY start_cluster)
{
    if (start_cluster < 2 || start_cluster > this->max_cluster_id)
    {
        return false;
    }
    FAT16_ENTRY free_cluster;
    while (start_cluster != 0xffff)
    {
        free_cluster = start_cluster;
        start_cluster = this->fat_table[start_cluster];
        this->fat_table[free_cluster] = 0x0000;
        this->free_cluster_ids.push_back(free_cluster);
    }
}

bool FAT16::read_cluster(FAT16_ENTRY cluster_id, char *buffer)
{
    uint32_t block_id;
    if (!this->LBA_to_PA(cluster_id, block_id))
    {
        return false;
    }
    for (uint32_t i = 0; i < this->DBR_512._BPB_.BPB_SecPerClus; ++i)
    {
        this->device->read_block(block_id + i, buffer + this->DBR_512._BPB_.BPB_BytsPerSec * i);
    }
    return true;
}

bool FAT16::write_cluster(FAT16_ENTRY cluster_id, const char *buffer)
{
    uint32_t block_id;
    if (!this->LBA_to_PA(cluster_id, block_id))
    {
        return false;
    }
    for (uint32_t i = 0; i < this->DBR_512._BPB_.BPB_SecPerClus; ++i)
    {
        this->device->write_block(block_id + i, buffer + this->DBR_512._BPB_.BPB_BytsPerSec * i);
    }
    return true;
}

FAT16::FAT16_ENTRY FAT16::follow_fat_chain(FAT16_ENTRY cluster_id)
{
    if (cluster_id < 2 || cluster_id > this->max_cluster_id)
    {
        return FAT16::INVALID_FAT16_ENTRY; // 返回无效表项
    }
    else
    {
        return this->fat_table[cluster_id];
    }
}

FAT16::FAT16_ENTRY FAT16::get_last_cluster_in_chain(FAT16_ENTRY start_cluster)
{
    if (start_cluster < 2 || start_cluster > this->max_cluster_id)
    {
        return FAT16::INVALID_FAT16_ENTRY; // 返回无效表项
    }
    while (this->fat_table[start_cluster] != 0xffff)
    {
        start_cluster = this->fat_table[start_cluster];
    }
    return start_cluster;
}

/********** Layer 2: 目录项操作 **********/

bool FAT16::find_entry(FAT16_ENTRY dir_cluster, const std::string &name, DIR_ENTRY &entry)
{
    std::string _name; // 由目录项 DIR_Name 转换来的 std::string 对象
    if (dir_cluster = FAT16::ROOT_DIR_CLUSTER)
    {
        for (std::vector<DIR_ENTRY>::iterator it = this->root_entry_table.begin(); it != this->root_entry_table.end(); ++it)
        {
            this->short_name_to_string(it->DIR_Name, _name);
            if (_name == name)
            {
                memcpy(&entry, &(*it), sizeof(DIR_ENTRY));
                return true;
            }
        }
        return false;
    }
    else
    {
        while (dir_cluster >= 2 && dir_cluster <= this->max_cluster_id)
        {
            if (!this->read_cluster(dir_cluster, reinterpret_cast<char *>(this->sub_dir_file.data())))
            {
                return false;
            }
            for (std::vector<DIR_ENTRY>::iterator it = this->sub_dir_file.begin(); it != this->sub_dir_file.end(); ++it)
            {
                this->short_name_to_string(it->DIR_Name, _name);
                if (_name == name)
                {
                    memcpy(&entry, &(*it), sizeof(DIR_ENTRY));
                    return true;
                }
            }
            dir_cluster = this->follow_fat_chain(dir_cluster);
        }
        return false;
    }
}

bool FAT16::add_entry(FAT16_ENTRY dir_cluster, const DIR_ENTRY &entry)
{
    if (dir_cluster == FAT16::ROOT_DIR_CLUSTER)
    {
        for (std::vector<DIR_ENTRY>::iterator it = this->root_entry_table.begin(); it != this->root_entry_table.end(); ++it)
        {
            if (it->DIR_Name[0] == 0xE5)
            {
                memcpy(&(*it), &entry, sizeof(DIR_ENTRY));
                return true;
            }
        }
        for (std::vector<DIR_ENTRY>::iterator it = this->root_entry_table.begin(); it != this->root_entry_table.end(); ++it)
        {
            if (it->DIR_Name[0] == 0x00)
            {
                memcpy(&(*it), &entry, sizeof(DIR_ENTRY));
                return true;
            }
        }
    }
    else
    {
        while (dir_cluster >= 2 && dir_cluster <= this->max_cluster_id)
        {

            if (!this->read_cluster(dir_cluster, reinterpret_cast<char *>(this->sub_dir_file.data())))
            {
                return false;
            }
            for (std::vector<DIR_ENTRY>::iterator it = this->sub_dir_file.begin(); it != this->sub_dir_file.end(); ++it)
            {
                if (it->DIR_Name[0] == 0xE5)
                {
                    memcpy(&(*it), &entry, sizeof(DIR_ENTRY));
                    if (!this->device->write_block(dir_cluster, reinterpret_cast<const char *>(this->sub_dir_file.data())))
                    {
                        return false;
                    }
                    return true;
                }
            }
            for (std::vector<DIR_ENTRY>::iterator it = this->sub_dir_file.begin(); it != this->sub_dir_file.end(); ++it)
            {
                if (it->DIR_Name[0] == 0x00)
                {
                    memcpy(&(*it), &entry, sizeof(DIR_ENTRY));
                    if (!this->device->write_block(dir_cluster, reinterpret_cast<const char *>(this->sub_dir_file.data())))
                    {
                        return false;
                    }
                    return true;
                }
            }
            dir_cluster = this->follow_fat_chain(dir_cluster);
        }
        return false;
    }
}

bool FAT16::remove_entry(FAT16_ENTRY dir_cluster, const std::string &name)
{
    std::string _name;
    if (dir_cluster == FAT16::ROOT_DIR_CLUSTER)
    {
        for (std::vector<DIR_ENTRY>::iterator it = this->root_entry_table.begin(); it != this->root_entry_table.end(); ++it)
        {
            this->short_name_to_string(it->DIR_Name, _name);
            if (_name == name)
            {
                it->DIR_Name[0] = 0xE5;
                return true;
            }
        }
        return false;
    }
    else
    {
        while (dir_cluster >= 2 && dir_cluster <= this->max_cluster_id)
        {
            if (!this->device->read_block(dir_cluster, reinterpret_cast<char *>(this->sub_dir_file.data())))
            {
                return false;
            }
            for (std::vector<DIR_ENTRY>::iterator it = this->sub_dir_file.begin(); it != this->sub_dir_file.end(); ++it)
            {
                this->short_name_to_string(it->DIR_Name, _name);
                if (_name == name)
                {
                    it->DIR_Name[0] = 0xE5;
                    return true;
                }
            }
            dir_cluster = this->follow_fat_chain(dir_cluster);
        }
        return false;
    }
}

std::vector<FAT16::DIR_ENTRY> FAT16::list_entries(FAT16_ENTRY dir_cluster)
{
    std::vector<DIR_ENTRY> list;
    list.clear();
    if (dir_cluster == FAT16::ROOT_DIR_CLUSTER)
    {
        for (std::vector<DIR_ENTRY>::iterator it = this->root_entry_table.begin(); it != this->root_entry_table.end(); ++it)
        {
            if (it->DIR_Name[0] != 0xE5 && it->DIR_Name[0] != 0x00)
            {
                list.push_back(*it);
            }
        }
        return list;
    }
    else
    {
        while (dir_cluster >= 2 && dir_cluster <= this->max_cluster_id)
        {
            if (!this->read_cluster(dir_cluster, reinterpret_cast<char *>(this->sub_dir_file.data())))
            {
                return list;
            }
            for (std::vector<DIR_ENTRY>::iterator it = this->sub_dir_file.begin(); it != this->sub_dir_file.end(); ++it)
            {
                if (it->DIR_Name[0] != 0xE5 && it->DIR_Name[0] != 0x00)
                {
                    list.push_back(*it);
                }
            }
            dir_cluster = this->follow_fat_chain(dir_cluster);
        }
        return list;
    }
}

/********** Layer 3: 路径解析 **********/

bool FAT16::resolve_path(const std::string &path, PathResult &result)
{   
    // 删除多余 '/'
    std::string path_copy = path;
    auto normalize = [](std::string &path_copy)
    {
        auto it = std::unique(path_copy.begin(), path_copy.end(),
                              [](char a, char b)
                              { return a == '/' && b == '/'; });
        path_copy.erase(it, path_copy.end());
    };
    normalize(path_copy);

    // 强制转大写
    std::for_each(path_copy.begin(), path_copy.end(), [](char &ch)
                  { ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch))); });

    // 检查文件名中非法字符
    if (path_copy.empty() || path_copy.find_first_of(" \"*/\\:<>?|") != std::string::npos) // '.'特殊处理
    {
        return false;
    }

    // 预处理为绝对路径的一系列文件名 token
    std::vector<std::string> tokens;
    tokens.clear();
    size_t start = 0, end = 0;
    while ((end = path_copy.find('/', start)) != std::string::npos)
    {
        tokens.push_back(path_copy.substr(start, end - start));
        if (tokens.back() == "..")
        {
            tokens.pop_back();
            tokens.pop_back();
        }
        else if (tokens.back() == ".")
        {
            tokens.pop_back();
        }
        start = end + 1;
    }
    
    // 特殊检查 '.'，以及文件名长度，不包括最后一个文件名
    for (std::vector<std::string>::iterator it = tokens.begin(); it != tokens.end() - 1; ++it)
    {
        if (it->find_first_of('.') != std::string::npos || it->size() > 11)
        {
            return false;
        }
    }
    // 对最后一项检查
    if (tokens.back().size() > 11)
    {
        return false;
    }
    // 具体的文件名合法性检查略去

    // 从根目录开始定位
    DIR_ENTRY temp_dir_ent;
    FAT16_ENTRY ent_cluster_id = FAT16::ROOT_DIR_CLUSTER, prev_cluster_id = FAT16::INVALID_FAT16_ENTRY;
    for (std::vector<std::string>::iterator it = tokens.begin(); it != tokens.end(); ++it)
    {   
        bool finded = this->find_entry(ent_cluster_id, *it, temp_dir_ent);
        if (!finded && it == tokens.end() - 1)
        {   
            result.parent_dir_cluster = prev_cluster_id;
            result.file_name = *it;
            result.exists = false;
            return true;
        }
        else if (!finded)
        {
            return false;
        }
        else 
        {
            prev_cluster_id = ent_cluster_id;
            ent_cluster_id = temp_dir_ent.DIR_FstClusLO;
        }
    }
    result.parent_dir_cluster = prev_cluster_id;
    result.file_name = tokens.empty() ? "/" : tokens.back();
    memcpy(&(result.entry), &temp_dir_ent, sizeof(DIR_ENTRY));
    result.exists = true;
}

/********** 辅助函数 **********/

uint32_t FAT16::get_total_clusters() const
{
    //  获取总扇区数
    uint32_t total_sectors = this->DBR_512._BPB_.BPB_TotSec16 > 0 ? this->DBR_512._BPB_.BPB_TotSec16 : this->DBR_512._BPB_.BPB_TotSec32;
    //  计算根目录所占扇区
    uint32_t total_root_ent_sectors = this->DBR_512._BPB_.BPB_RootEntCnt * 32 /
                                      this->DBR_512._BPB_.BPB_BytsPerSec;
    // 计算总数据/子目录扇区数
    uint32_t total_data_sectors = total_sectors -
                                  this->DBR_512._BPB_.BPB_RsvdSecCnt -
                                  this->DBR_512._BPB_.BPB_FATSz16 * this->DBR_512._BPB_.BPB_NumFATs -
                                  total_root_ent_sectors;
    //  计算返回总簇数
    return total_data_sectors / this->DBR_512._BPB_.BPB_SecPerClus;
}

bool FAT16::LBA_to_PA(uint32_t cluster_id, uint32_t &block_id) const
{
    //  簇号范围：[2, MAX] ，在FAT16下，MAX = 总簇数 + 1
    if (cluster_id < 2 || cluster_id > this->get_total_clusters() + 1)
    {
        return false;
    }
    else
    {
        //  计算首簇的物理地址
        uint32_t first_cluster_block_id = this->DBR_512._BPB_.BPB_RsvdSecCnt +
                                          this->DBR_512._BPB_.BPB_NumFATs * this->DBR_512._BPB_.BPB_FATSz16 +
                                          this->DBR_512._BPB_.BPB_RootEntCnt * 32 / this->DBR_512._BPB_.BPB_BytsPerSec;
        block_id = first_cluster_block_id + (cluster_id - 2) * this->DBR_512._BPB_.BPB_SecPerClus; //  套用公式获得簇物理地址
        return true;
    }
}

void FAT16::validation_fat16(const std::string &disk_img)
{

    // 检验每个扇区的字节数的合法性
    uint16_t BPS = this->DBR_512._BPB_.BPB_BytsPerSec;
    if (!(BPS == 512 || BPS == 1024 || BPS == 2048 || BPS == 4096))
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": BPB_BytsPerSec = "
                  << BPS << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    // 检验保留区域的扇区数量
    if (this->DBR_512._BPB_.BPB_RsvdSecCnt == 0)
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": BPB_RsvdSecCnt = "
                  << this->DBR_512._BPB_.BPB_RsvdSecCnt << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    //  检验16位长度卷的总扇区数和32位长度卷的总扇区数
    uint32_t TS16 = this->DBR_512._BPB_.BPB_TotSec16,
             TS32 = this->DBR_512._BPB_.BPB_TotSec32;
    if (!((TS16 < 0x10000 && TS32 == 0) || (TS16 == 0 && TS32 >= 0x10000)))
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": BPB_TotSec16 = "
                  << TS16
                  << ", BPB_TotSec32 = "
                  << TS32
                  << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    //  检验单个FAT表占用的扇区数
    if (this->DBR_512._BPB_.BPB_FATSz16 == 0)
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": BPB_FATSz16 = "
                  << this->DBR_512._BPB_.BPB_FATSz16 << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    // 检验用于0x13中断的驱动器号
    if (!(this->DBR_512._BS_END_.BS_DrvNum == 0x80 || this->DBR_512._BS_END_.BS_DrvNum == 0x00))
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": BS_DrvNum = 0x"
                  << std::setw(2) << std::setfill('0') << std::hex
                  << static_cast<uint32_t>(this->DBR_512._BS_END_.BS_DrvNum) << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    // 检验保留位
    if (this->DBR_512._BS_END_.BS_Reserved1 != 0)
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": BS_Reserved1 = "
                  << static_cast<uint32_t>(this->DBR_512._BS_END_.BS_Reserved1) << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    // 检验启动扇区的完整性的签名（松散检测）
    if (DBR_512._BS_END_.BS_BootSig != 0x28 && DBR_512._BS_END_.BS_BootSig != 0x29)
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": BS_BootSig = 0x"
                  << std::setw(2) << std::setfill('0') << std::hex
                  << DBR_512._BS_END_.BS_BootSig << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    // 检验空余（小于512字节）
    for (int i = 0; i < 448; ++i)
    {
        if (this->DBR_512.DBR_Zero[i] != 0)
        {
            std::cerr << "Invalid disk image \"" << disk_img
                      << "\": Dirty DBR " << "\nAbort.\n";
            exit(EXIT_FAILURE);
        }
    }

    // 检验校验位
    if (this->DBR_512.Signature_word != 0xAA55)
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": Signature_word = 0x"
                  << std::setw(4) << std::setfill('0') << std::hex
                  << this->DBR_512.Signature_word << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    // 检验空余（大于512字节）
    if (this->DBR_512._BPB_.BPB_BytsPerSec > 512)
    {
        std::ifstream disk_in(disk_img, std::ios_base::binary | std::ios_base::in);
        if (!disk_in.is_open())
        {
            std::cerr << "Cannot open disk image \"" << disk_img
                      << "\"\nAobrt.\n";
            exit(EXIT_FAILURE);
        }

        int16_t rest_zero_bytes = this->DBR_512._BPB_.BPB_BytsPerSec - 512;
        std::vector<uint8_t> datas(rest_zero_bytes);
        disk_in.seekg(512, std::ios::beg);
        disk_in.read(reinterpret_cast<char *>(datas.data()), rest_zero_bytes);
        disk_in.close();

        for (int16_t i = 0; i < rest_zero_bytes; ++i)
        {
            if (datas[i] != 0)
            {
                std::cerr << "Invalid disk image \"" << disk_img
                          << "\": Dirty DBR " << "\nAbort.\n";
                exit(EXIT_FAILURE);
            }
        }
    }

    //  FAT[0]检验
    int16_t sign_ext_media = static_cast<int8_t>(this->DBR_512._BPB_.BPB_Media);
    if (static_cast<uint16_t>(sign_ext_media) != this->fat_table[0])
    {
        std::cerr << "Invalid disk image \"" << disk_img
                  << "\": BPB_Media = 0x"
                  << std::setw(2) << std::setfill('0') << std::hex
                  << static_cast<uint32_t>(this->DBR_512._BPB_.BPB_Media)
                  << ", FAT[0] = 0x"
                  << std::setw(4) << std::setfill('0') << std::hex
                  << this->fat_table[0]
                  << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    // 检测簇数是否为 FAT16 规定合法值
    uint32_t total_clus = this->get_total_clusters();
    if (!(total_clus >= 4085 && total_clus < 65525))
    {
        std::cerr << "\"" << disk_img
                  << "\"may be FAT12 or FAT32 image, total clusters = "
                  << total_clus << "\nAbort.\n";
        exit(EXIT_FAILURE);
    }

    //  检测根目录中卷标文件与 DBR 中卷标的一致性（严格检测）
    if (this->DBR_512._BS_END_.BS_BootSig == 0x29)
    {
        std::vector<DIR_ENTRY>::iterator VolLab_it;
        for (VolLab_it = this->root_entry_table.begin(); VolLab_it != this->root_entry_table.end(); ++VolLab_it)
        {
            if (VolLab_it->DIR_Attr == 0x08)
            {
                break;
            }
        }
        if (VolLab_it == this->root_entry_table.end())
        {
            std::cerr << "Invalid disk image \"" << disk_img
                      << "\": Without volume label file in the root directory"
                      << "\nAbort.\n";
            exit(EXIT_FAILURE);
        }
        for (int i = 0; i < 11; ++i)
        {
            if (this->DBR_512._BS_END_.BS_VolLab[i] != VolLab_it->DIR_Name[i])
            {
                std::cerr << "Invalid disk image \"" << disk_img
                          << "\": BS_VolLab[" << i << "] = 0x"
                          << std::setw(2) << std::setfill('0') << std::hex
                          << static_cast<uint32_t>(this->DBR_512._BS_END_.BS_VolLab[i])
                          << ", DIR_Name[" << i << "] = 0x"
                          << std::setw(2) << std::setfill('0') << std::hex
                          << VolLab_it->DIR_Name[i]
                          << "\nAbort.\n";
                exit(EXIT_FAILURE);
            }
        }
    }
}