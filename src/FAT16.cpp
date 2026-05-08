#include "../include/FAT16.hpp"
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
    for (FAT16_ENTRY i = 2; i <= max_cluster_id; ++i)
    {
        if (this->fat_table[i] == 0x0000)
        {
            this->free_cluster_ids.push_back(i);
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
    this->pwd_cluster_id = FAT16::ROOT_DIR_CLUSTER;

    //  子目录文件初始化大小为1簇
    this->sub_dir_file.resize(static_cast<uint32_t>(this->DBR_512._BPB_.BPB_BytsPerSec) * static_cast<uint32_t>(this->DBR_512._BPB_.BPB_SecPerClus) / sizeof(DIR_ENTRY));

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
    fout.seekp(this->DBR_512._BPB_.BPB_BytsPerSec * this->DBR_512._BPB_.BPB_RsvdSecCnt, std::ios::beg);
    fout.write(reinterpret_cast<const char *>(this->fat_table.data()), this->fat_table.size() * sizeof(FAT16_ENTRY));
    if (this->DBR_512._BPB_.BPB_NumFATs == 2)
    {
        fout.write(reinterpret_cast<const char *>(this->fat_table.data()), this->fat_table.size() * sizeof(FAT16_ENTRY));
    }
    fout.write(reinterpret_cast<const char *>(this->root_entry_table.data()), this->root_entry_table.size() * sizeof(DIR_ENTRY));

    std::cout << "\"" << this->disk_name << "\" unmonted.\n";
}

/********** ### Layer 4: 文件/目录操作（面向用户的 API） **********/

bool FAT16::create_file(const std::string &path)
{
    std::string normalized_path;
    // 检查路径格式正确性
    if (!this->path_normalizer(path, normalized_path))
    {
        std::cerr << "Syntax error: Invalid path!\n";
        return false;
    }
    PATH_RESULT path_result_info;
    // 检查路径存在否
    if (!this->resolve_path(normalized_path, path_result_info))
    {
        std::cerr << "Error: Path does not exist!\n"
                  << "path: " << normalized_path << "\n";
        return false;
    }
    //  重名检查
    if (path_result_info.exists)
    {
        std::cerr << "Error: file \"" << path << "\" already exsits!\n";
        return false;
    }
    // 检查是否末尾有 '/'
    if (path.back() == '/')
    {
        std::cerr << "Sytax error: Empty filename!\n";
        return false;
    }

    // 获取文件名
    std::string _name, _ext;
    this->get_splited_dir_name(normalized_path, _name, _ext);
    std::string filename = _ext.empty() ? _name : _name + '.' + _ext;
    if (filename == "." || filename == "..")
    {
        std::cerr << "Error: Reserved filename: " << filename << "\n";
        return false;
    }

    // 创建目录项，开始填写文件元数据
    DIR_ENTRY new_dir_ent;
    // 填写 DIR_Name
    memset(new_dir_ent.DIR_Name, ' ', 11);
    for (size_t i = 0; i < 8 && i < _name.size(); ++i)
    {
        new_dir_ent.DIR_Name[i] = _name[i];
    }
    for (size_t i = 8; i < 11 && i - 8 < _ext.size(); ++i)
    {
        new_dir_ent.DIR_Name[i] = _ext[i - 8];
    }
    new_dir_ent.DIR_Attr = 0x00;
    new_dir_ent.DIR_NTRes = 0x00;
    // 日期相关信息无关紧要暂时忽略
    // new_dir_ent.DIR_CrtTimeTenth
    // new_dir_ent.DIR_CrtTime
    // new_dir_ent.DIR_CrtDate
    // new_dir_ent.DIR_LstAccDate
    // new_dir_ent.DIR_WrtTime
    // new_dir_ent.DIR_WrtDate

    new_dir_ent.DIR_FstClusHI = 0x0000; // FAT16 下无用，但考虑镜像向前兼容置为0
    new_dir_ent.DIR_FstClusLO = 0xffff; // 空文件
    new_dir_ent.DIR_FileSize = 0;
    // 目录项/文件元数据填写完毕

    if (!this->add_entry(path_result_info.parent_dir_cluster, new_dir_ent))
    {
        std::cerr << "Error: Insufficient FCBs\n";
        return false;
    }
    else
    {
        return true;
    }
}

bool FAT16::delete_file(const std::string &path)
{
    std::string normalized_path;
    // 检查路径格式正确性
    if (!this->path_normalizer(path, normalized_path))
    {
        std::cerr << "Syntax error: Invalid path!\n";
        return false;
    }
    PATH_RESULT path_result_info;
    // 检查路径存在否
    if (!this->resolve_path(normalized_path, path_result_info))
    {
        std::cerr << "Error: Path does not exist!\n"
                  << "path: " << normalized_path << "\n";
        return false;
    }
    else
    { // 文件不存在不能删除
        if (!path_result_info.exists)
        {
            std::cerr << "Error: file does not exist!\n";
            return false;
        }
    }
    // 检查是否末尾有 '/'
    if (path.back() == '/')
    {
        std::cerr << "Sytax error: Empty filename!\n";
        return false;
    }
    // 实际是目录，不能删除
    if (path_result_info.entry.DIR_Attr == 0x10)
    {
        std::cerr << "Error: \"" << path << "\" is not a file!\n";
        return false;
    }

    // 获取文件名
    std::string _name, _ext, filename;
    this->get_splited_dir_name(normalized_path, _name, _ext);
    filename = _ext.empty() ? _name : (_name + '.' + _ext);

    // 开始删除文件
    // step 1: 回收簇
    // step 2: 回收目录项
    if (path_result_info.entry.DIR_FstClusLO != 0xffff)
    {
        this->free_cluster_chain(path_result_info.entry.DIR_FstClusLO);
    }
    this->remove_entry(path_result_info.parent_dir_cluster, filename);

    return true;
}

bool FAT16::read_file(const std::string &path, std::vector<char> &buffer)
{
    std::string normalized_path;
    // 检查路径格式正确性
    if (!this->path_normalizer(path, normalized_path))
    {
        std::cerr << "Syntax error: Invalid path!\n";
        return false;
    }
    PATH_RESULT path_result_info;
    // 检查路径存在否
    if (!this->resolve_path(normalized_path, path_result_info))
    {
        std::cerr << "Error: Path does not exist!\n"
                  << "path: " << normalized_path << "\n";
        return false;
    }
    else
    { // 文件不存在不能读取
        if (!path_result_info.exists)
        {
            std::cerr << "Error: file does not exist!\n";
            return false;
        }
    }
    // 检查是否末尾有 '/'
    if (path.back() == '/')
    {
        std::cerr << "Sytax error: Empty filename!\n";
        return false;
    }
    // 实际是目录，不能读取
    if (path_result_info.entry.DIR_Attr == 0x10)
    {
        std::cerr << "Error: \"" << path << "\" is not a file!\n";
        return false;
    }

    // 开始读取文件
    uint32_t remaining_bytes = path_result_info.entry.DIR_FileSize;
    uint32_t cluster_size = static_cast<uint32_t>(this->DBR_512._BPB_.BPB_BytsPerSec) * static_cast<uint32_t>(this->DBR_512._BPB_.BPB_SecPerClus);
    buffer.resize((remaining_bytes - 1 + cluster_size) / cluster_size * cluster_size); // 先将缓冲大小做成总簇数
    FAT16_ENTRY reading_cluster_id = path_result_info.entry.DIR_FstClusLO;
    size_t cluster_count = 0;
    while (remaining_bytes > 0 && reading_cluster_id != 0xffff)
    {
        if (!this->read_cluster(reading_cluster_id, buffer.data() + cluster_size * cluster_count))
        {
            std::cerr << "Error: Cannot read file";
            return false;
        }
        reading_cluster_id = follow_fat_chain(reading_cluster_id);
        remaining_bytes -= cluster_size;
        ++cluster_count;
    }
    buffer.resize(path_result_info.entry.DIR_FileSize); // 再将缓冲大小设置为文件大小
    return true;
}

bool FAT16::write_file(const std::string &path, const std::vector<char> &data)
{
    // 计算文件所需簇
    uint32_t cluster_size = static_cast<uint32_t>(this->DBR_512._BPB_.BPB_BytsPerSec) * static_cast<uint32_t>(this->DBR_512._BPB_.BPB_SecPerClus);
    uint32_t clusters_need = (data.size() - 1 + cluster_size) / cluster_size;
    if (this->free_cluster_ids.size() < clusters_need)
    {
        std::cerr << "Error: Insufficient image storage capacity!\n";
        return false;
    }

    // 以下开始查询文件FCB

    std::string normalized_path;
    // 检查路径格式正确性
    if (!this->path_normalizer(path, normalized_path))
    {
        std::cerr << "Syntax error: Invalid path!\n";
        return false;
    }
    PATH_RESULT path_result_info;
    // 检查路径存在否
    if (!this->resolve_path(normalized_path, path_result_info))
    {
        std::cerr << "Error: Path does not exist!\n"
                  << "path: " << normalized_path << "\n";
        return false;
    }
    else
    {
        // 文件不存在不能覆写
        if (!path_result_info.exists)
        {
            std::cerr << "Error: file does not exist!\n";
            return false;
        }
    }
    // 检查是否末尾有 '/'
    if (path.back() == '/')
    {
        std::cerr << "Sytax error: empty filename!\n";
        return false;
    }
    // 实际是目录，不能覆写
    if (path_result_info.entry.DIR_Attr == 0x10)
    {
        std::cerr << "Error: \"" << path << "\" is not a file!\n";
        return false;
    }

    // 先写入文件数据
    std::vector<char> dup_data = data;
    dup_data.resize(clusters_need * cluster_size); // 不必关心垃圾值
    FAT16_ENTRY start_cluster_id = this->alloc_cluster();
    FAT16_ENTRY prev_cluster_id, curr_cluster_id = start_cluster_id;
    for (size_t i = 0; i < clusters_need; ++i)
    {
        if (i != 0)
        {
            prev_cluster_id = curr_cluster_id;
            curr_cluster_id = this->alloc_cluster();
            this->fat_table[prev_cluster_id] = curr_cluster_id;
        }
        if (!this->write_cluster(curr_cluster_id, dup_data.data() + i * cluster_size))
        {
            // 回滚
            this->free_cluster_chain(start_cluster_id);
            std::cerr << "Error: file truct & write failed!\n";
            return false;
        }
    }
    path_result_info.entry.DIR_FileSize = data.size();

    // 再修改目录项
    // 先移除原本目录项，再添加这个目录项
    this->remove_entry(path_result_info.parent_dir_cluster, path_result_info.parent_filename);
    this->free_cluster_chain(path_result_info.entry.DIR_FstClusLO); // 回收旧数据占用空间
    path_result_info.entry.DIR_FstClusLO = start_cluster_id;        // 修改起始簇号
    this->add_entry(path_result_info.parent_dir_cluster, path_result_info.entry);
    return true;
}

bool FAT16::create_dir(const std::string &path)
{
    // 无可用空间不创建目录，因为创建目录必然创建子目录
    if (this->free_cluster_ids.size() == 0)
    {
        std::cerr << "Error: Insufficient image storage capacity!\n";
        return false;
    }

    std::string normalized_path;
    // 检查路径格式正确性
    if (!this->path_normalizer(path, normalized_path))
    {
        std::cerr << "Syntax error: Invalid path!\n";
        return false;
    }
    PATH_RESULT path_result_info;
    // 检查路径存在否
    if (!this->resolve_path(normalized_path, path_result_info))
    {
        std::cerr << "Error: Path does not exist!\n"
                  << "path: " << normalized_path << "\n";
        return false;
    }
    else
    {
        // 重名检查
        if (path_result_info.exists)
        {
            std::cerr << "Error: File/directory already exists!\n";
            return false;
        }
    }

    // 获取目录名
    std::string _name, _ext;
    this->get_splited_dir_name(normalized_path, _name, _ext);
    if (!_ext.empty())
    {
        std::cerr << "Error: Invalid directory name with '.'!\n";
        return false;
    }
    if (_name == "." || _name == "..")
    {
        std::cerr << "Error: Reserved directory name: " << _name << "\n";
        return false;
    }

    // 生成当前目录项并加入父目录文件
    FAT16_ENTRY pwd_id = this->alloc_cluster();
    DIR_ENTRY pwd_dir_ent;
    memset(pwd_dir_ent.DIR_Name, ' ', 11);
    for (size_t i = 0; i < _name.size(); ++i)
    {
        pwd_dir_ent.DIR_Name[i] = _name[i];
    }
    pwd_dir_ent.DIR_Attr = 0x10;
    pwd_dir_ent.DIR_NTRes = 0x00;

    // 忽略时间相关字段，以下相同

    pwd_dir_ent.DIR_FstClusHI = 0x0000;
    pwd_dir_ent.DIR_FstClusLO = pwd_id;
    pwd_dir_ent.DIR_FileSize = 0; // 不关心目录文件大小，反正是整簇大小
    this->add_entry(path_result_info.parent_dir_cluster, pwd_dir_ent);

    // 建立目录文件
    std::for_each(this->sub_dir_file.begin(), this->sub_dir_file.end(), [](DIR_ENTRY &de)
                  { de.DIR_Name[0] = 0x00; });
    // 填写 ".." 与 "." 目录项
    // 第一项 "."
    memset(&this->sub_dir_file[0], ' ', 11);
    this->sub_dir_file[0].DIR_Name[0] = '.';
    this->sub_dir_file[0].DIR_Attr = 0x10;
    this->sub_dir_file[0].DIR_NTRes = 0;
    this->sub_dir_file[0].DIR_FstClusHI = 0x0000;
    this->sub_dir_file[0].DIR_FstClusLO = pwd_id;
    this->sub_dir_file[0].DIR_FileSize = 0;
    // 第二项 ".."
    memset(&this->sub_dir_file[1], ' ', 11);
    this->sub_dir_file[1].DIR_Name[0] = '.';
    this->sub_dir_file[1].DIR_Name[1] = '.';
    this->sub_dir_file[1].DIR_Attr = 0x10;
    this->sub_dir_file[1].DIR_NTRes = 0;
    this->sub_dir_file[1].DIR_FstClusHI = 0x0000;
    this->sub_dir_file[1].DIR_FstClusLO = path_result_info.parent_dir_cluster;
    this->sub_dir_file[1].DIR_FileSize = 0;
    // 写回目录文件
    this->write_cluster(pwd_id, reinterpret_cast<char *>(this->sub_dir_file.data()));
    return true;
}

bool FAT16::remove_dir(const std::string &path)
{
    std::string normalized_path;
    // 检查路径格式正确性
    if (!this->path_normalizer(path, normalized_path))
    {
        std::cerr << "Syntax error: Invalid path!\n";
        return false;
    }
    PATH_RESULT path_result_info;
    // 检查路径存在否
    if (!this->resolve_path(normalized_path, path_result_info))
    {
        std::cerr << "Error: Path does not exist!\n"
                  << "path: " << normalized_path << "\n";
        return false;
    }
    else
    {
        // 不存在该目录
        if (!path_result_info.exists)
        {
            std::cerr << "Error: file does not exist!\n";
            return false;
        }
    }
    // 实际是文件，不能删除
    if (path_result_info.entry.DIR_Attr == 0x00)
    {
        std::cerr << "Error: \"" << path << "\" is not a directory!\n";
        return false;
    }

    // 尝试移除目录，注意当前目录是空目录才可移除
    // 实现中
}

std::vector<FAT16::DIR_ENTRY> FAT16::list_dir(const std::string &path)
{
    // 暂时不实现
    std::cerr << "Error: Not implemented!\n";
    return std::vector<FAT16::DIR_ENTRY>(0);
}

bool FAT16::export_file(const std::string &src_path, const std::string &dest_path)
{
    // 暂时不实现
    std::cerr << "Error: Not implemented!\n";
    return false;
}

bool FAT16::load_file(const std::string &src_path, const std::string &dest_path)
{
    // 暂时不实现
    std::cerr << "Error: Not implemented!\n";
    return false;
}

/********** Layer 1: 簇操作 **********/

FAT16::FAT16_ENTRY FAT16::alloc_cluster()
{
    if (this->free_cluster_ids.empty())
    {
        return FAT16::INVALID_FAT16_ENTRY;
    }
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
    return true;
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
    if (dir_cluster == FAT16::ROOT_DIR_CLUSTER)
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
        // 根目录已满
        return false;
    }
    else
    {
        FAT16_ENTRY prev_cluster_id; // 后面会被先赋值
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
                    if (!this->write_cluster(dir_cluster, reinterpret_cast<const char *>(this->sub_dir_file.data())))
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
                    if (!this->write_cluster(dir_cluster, reinterpret_cast<const char *>(this->sub_dir_file.data())))
                    {
                        return false;
                    }
                    return true;
                }
            }
            prev_cluster_id = dir_cluster;
            dir_cluster = this->follow_fat_chain(dir_cluster);
        }

        // 子目录文件扩张
        if (this->free_cluster_ids.empty()) // 存储空间已慢
        {
            return false;
        }
        else
        {
            dir_cluster = this->alloc_cluster();
            this->fat_table[prev_cluster_id] = dir_cluster; // 构造目录文件链
            std::for_each(this->sub_dir_file.begin(), this->sub_dir_file.end(), [](DIR_ENTRY &de)
                          { de.DIR_Name[0] = 0x00; });
            memcpy(&(this->sub_dir_file[0]), &entry, sizeof(DIR_ENTRY));
            if (!this->write_cluster(dir_cluster, reinterpret_cast<char *>(this->sub_dir_file.data())))
            {
                return false;
            }
        }
        return true;
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
            if (!this->read_cluster(dir_cluster, reinterpret_cast<char *>(this->sub_dir_file.data())))
            {
                return false;
            }
            for (std::vector<DIR_ENTRY>::iterator it = this->sub_dir_file.begin(); it != this->sub_dir_file.end(); ++it)
            {
                this->short_name_to_string(it->DIR_Name, _name);
                if (_name == name)
                {
                    it->DIR_Name[0] = 0xE5;
                    this->write_cluster(dir_cluster, reinterpret_cast<const char *>(this->sub_dir_file.data()));
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

std::string FAT16::assist_normalize(const std::string &path) const
{
    std::string normalized_path = path;
    std::string::iterator it = std::unique(normalized_path.begin(), normalized_path.end(), [](char a, char b)
                                           { return a == '/' && b == '/'; });
    normalized_path.erase(it, normalized_path.end());
    std::for_each(normalized_path.begin(), normalized_path.end(), [](char &ch)
                  { ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch))); });
    return normalized_path;
}

bool FAT16::validation_name(const std::string &name) const
{
    if (name.empty() || name.find_first_of(" \"*/\\:<>?|") != std::string::npos || name.size() > 12)
    {
        return false;
    }
    size_t dot_pos = name.find_first_of('.');
    if (dot_pos != std::string::npos && name != "." && name != "..")
    {
        if (name.find('.', dot_pos + 1) != std::string::npos) // 不允许多个不连续 '.'
        {
            return false;
        }
        else
        {
            // 若含有 '.'，即 dot_pos <= 8，则条件为 !(名字长度小于等于8且拓展名小于等于3) 则名字不合法
            if (!(dot_pos <= 8 && name.size() - dot_pos - 1 <= 3))
            {
                return false;
            }
        }
    }
    else if (name.size() > 8) // 无 '.' 即无拓展名时文件名最长不超过8
    {
        return false;
    }
    return true;
}

bool FAT16::path_normalizer(const std::string &path, std::string &normalized_path) const
{
    normalized_path = this->assist_normalize(path);
    if (normalized_path.find('/') == std::string::npos) // 无 '/' 单名字 token
    {
        return this->validation_name(normalized_path);
    }
    size_t start = 0, end = 0;
    while ((end = normalized_path.find('/', start)) != std::string::npos)
    {
        if (end - start > 0)
        {
            if (!this->validation_name(normalized_path.substr(start, end - start)))
            {
                return false;
            }
        }
        start = end + 1;
    }
    if (start != normalized_path.size()) // 检查最后一个无 '/' 结尾的 token
    {
        return this->validation_name(normalized_path.substr(start, normalized_path.size()));
    }
    return true;
}

bool FAT16::path_simplify(const std::string &normalized_path, std::string &simplified_path) const
{
    std::vector<std::string> tokens;
    tokens.clear();
    size_t start = 0, end = 0;
    if (normalized_path.find('/') == std::string::npos) // 单名字无任何 '/'
    {
        simplified_path = normalized_path;
        return true;
    }
    while ((end = normalized_path.find('/', start)) != std::string::npos)   // 这个 while 条件无法加入 "/" 根目录
    {
        if (end - start > 0)
        {
            tokens.push_back(normalized_path.substr(start, end - start));
            if (tokens.back() == ".")
            {
                tokens.pop_back();
            }
            else if (tokens.back() == "..")
            {
                if (tokens.size() >= 2) // POSIX 语义认为 "/.." 等能解析到根目录的父目录为 "/" 即根目录
                {
                    tokens.pop_back();
                }
                tokens.pop_back();
            }
        }
        start = end + 1;
    }
    if (start != normalized_path.size())
    {
        tokens.push_back(normalized_path.substr(start, normalized_path.size()));
        if (tokens.back() == ".")
        {
            tokens.pop_back();
        }
        else if (tokens.back() == "..")
        {
            if (tokens.size() >= 2) // POSIX 语义认为 "/.." 等能解析到根目录的父目录为 "/" 即根目录
            {
                tokens.pop_back();
            }

            tokens.pop_back();
        }
    }
    simplified_path.clear();
    std::for_each(tokens.begin(), tokens.end(), [&simplified_path](std::string &token)  // 由于 tokens 不含 "/" 根目录，所以下一 if 语句特殊处理化简路径为 "/" 根目录的简化路径
                  { simplified_path += "/"; simplified_path += token; });
    if (simplified_path.empty())
    {
        simplified_path = "/";
    }
    return true;
}

bool FAT16::resolve_path(const std::string &normalized_path, PATH_RESULT &result)
{
    // 转为绝对路径
    std::string complete_normalized_path = normalized_path[0] == '/' ? normalized_path : this->pwd + normalized_path;
    // 简化绝对路径
    std::string simplified_path;
    if (!this->path_simplify(complete_normalized_path, simplified_path))
    {
        // path_simplify 有输出
        return false;
    }
    // 预处理为绝对路径名的一系列文件名 token，注意不包含根目录
    std::vector<std::string> tokens;
    tokens.clear();
    if (simplified_path.find('/') == std::string::npos)
    {
        tokens.push_back(simplified_path);
    }
    size_t start = 0, end = 0;
    while ((end = simplified_path.find('/', start)) != std::string::npos)
    {
        if (end - start > 0) // 不将空串加入 tokens，并且不会加入根目录
        {
            tokens.push_back(simplified_path.substr(start, end - start));
        }
        start = end + 1;
    }
    if (start != simplified_path.size() && simplified_path != "/")  // 路径最后一级如无 '/' 也要加入 tokens
    {
        tokens.push_back(simplified_path.substr(start, simplified_path.size()));
    }

    // 首先找到起始簇号与相应前驱簇号，第一个 token 是没有在 tokens 中的 "/" 根目录
    DIR_ENTRY temp_dir_ent;
    memset(&temp_dir_ent, 0, sizeof(DIR_ENTRY));
    FAT16_ENTRY par_cluster_id = FAT16::ROOT_DIR_CLUSTER;
    FAT16_ENTRY save_par_cluster_id = FAT16::ROOT_DIR_CLUSTER; // 记录父目录簇

    // 定位文件，按名查找
    for (std::vector<std::string>::iterator it = tokens.begin(); it != tokens.end(); ++it)
    {
        bool finded = this->find_entry(par_cluster_id, *it, temp_dir_ent);
        if (!finded && it == tokens.end() - 1) // 只有最后一级无法解析
        {
            result.parent_dir_cluster = par_cluster_id;
            result.parent_filename = it == tokens.begin() ? "/" : *it;
            result.exists = false;
            return true;
        }
        else if (!finded)
        {
            return false;
        }
        else
        {
            // 进入下一个 token 前保存当前簇（即下一级的父目录）
            save_par_cluster_id = par_cluster_id;
            par_cluster_id = temp_dir_ent.DIR_FstClusLO;
        }
    }
    if (tokens.empty()) // 这表明解析结果是根目录
    {
        result.parent_dir_cluster = FAT16::INVALID_FAT16_ENTRY; // 根目录没有父目录所以父目录簇号为无效值
        result.parent_filename = ""; // 根目录没有父目录所以父目录名称为空
        memset(&result.entry, 0, sizeof(DIR_ENTRY)); // 根目录没有 entry
        result.entry.DIR_FstClusLO = FAT16::ROOT_DIR_CLUSTER;   // 根目录的约定簇号有意义故仍然设置，增加上级调用判断的方式
    }
    else
    {
        result.parent_dir_cluster = save_par_cluster_id; // 使用父目录簇，而非文件自身簇
        result.parent_filename = tokens.back(); // token 生成过程使得每个名称都不会含有 '/'，名称后的 '/' 合法性由上层语义层判断
        memcpy(&(result.entry), &temp_dir_ent, sizeof(DIR_ENTRY));
    }
    result.exists = true;
    return true;
}

/********** 辅助函数 **********/

uint32_t FAT16::get_total_clusters() const
{
    //  获取总扇区数
    uint32_t total_sectors = static_cast<uint32_t>(this->DBR_512._BPB_.BPB_TotSec16) > 0 ? static_cast<uint32_t>(this->DBR_512._BPB_.BPB_TotSec16) : static_cast<uint32_t>(this->DBR_512._BPB_.BPB_TotSec32);
    //  计算根目录所占扇区
    uint32_t total_root_ent_sectors = static_cast<uint32_t>(this->DBR_512._BPB_.BPB_RootEntCnt) * 32 /
                                      static_cast<uint32_t>(this->DBR_512._BPB_.BPB_BytsPerSec);
    // 计算总数据/子目录扇区数
    uint32_t total_data_sectors = total_sectors -
                                  this->DBR_512._BPB_.BPB_RsvdSecCnt -
                                  static_cast<uint32_t>(this->DBR_512._BPB_.BPB_FATSz16) * static_cast<uint32_t>(this->DBR_512._BPB_.BPB_NumFATs) -
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

void FAT16::short_name_to_string(const uint8_t *DIR_Name, std::string &name)
{
    // 处理空闲/已删除/无效目录项（由上层维护，无效目录项不会传入到此）
    if (DIR_Name[0] == 0x00 || DIR_Name[0] == 0xE5)
    {
        name = "";
        return;
    }
    std::string _name, _ext;
    _name.clear();
    _ext.clear();
    size_t i = 0;
    for (; i < 8 && DIR_Name[i] != ' '; ++i)
    {
        _name += DIR_Name[i];
    }
    while (DIR_Name[i] == ' ' && i < 11)
    {
        ++i;
    }
    for (; i < 11 && DIR_Name[i] != ' '; ++i)
    {
        _ext += DIR_Name[i];
    }
    name = _ext.empty() ? _name : (_name + '.' + _ext);
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

void FAT16::get_splited_dir_name(const std::string &normalized_path, std::string &_name, std::string &_ext) const
{
    size_t name_start = normalized_path.find_last_of('/');
    size_t last_dot = normalized_path.find_last_of('.');
    if (name_start == std::string::npos)    // 无 '/'
    {
        if (last_dot == std::string::npos)  // 无 '.'
        {
            _name = normalized_path;    // 所以 DIR_Name 的 std::string 就是这个，如 normalized_path = "DIR1"
            _ext = "";
        }
        else    // 有 '.'，如 normalized_path = "a.bc"
        {
            _name = normalized_path.substr(0, last_dot);
            _ext = normalized_path.substr(last_dot + 1, normalized_path.size() - last_dot - 1);
        }
    }
    else    // 有 '/' 的路径，即如 normalized_path = "/DIR1/DIR2" 或 normalized_path = "DIR1/DIR2.XX"
    {
        if (last_dot == std::string::npos || last_dot < name_start) // 完全没有 '.' 或者 '.' 在最后一个文件名称 token （路径最后一级）之前
        {
            _name = normalized_path.substr(name_start + 1, normalized_path.size() - name_start - 1);
            _ext = "";
        }
        else    // 否则路径最后一级是具有 '.' 分割开来的 _name + '.' + _ext 形式
        {
            _name = normalized_path.substr(name_start + 1, last_dot - name_start - 1);
            _ext = normalized_path.substr(last_dot + 1, normalized_path.size() - last_dot - 1);
        }
    }
}