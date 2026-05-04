#include "../include/FAT16.hpp"
#include "../include/BlockDevice.hpp"

FAT16::FAT16(const std::string &disk_img) : disk_name(disk_img)
{
    //  读取DBR
    std::ifstream disk_in(disk_img, std::ios_base::binary);
    if (!disk_in.is_open())
    {
        std::cerr << "Cannot open disk image \"" << disk_img
                  << "\"\nAobrt.\n";
        exit(EXIT_FAILURE);
    }
    disk_in.read(reinterpret_cast<char *>(&this->DBR_512), 512); //  如果 BPB_BytsPerSec > 512 则存在此域，全部置零，故只需读入起始512字节

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

    //  检验根目录中的条目数    无用代码注释掉
    // if (32 * this->DBR_512._BPB_.BPB_RootEntCnt % 2 != 0)
    //{
    //    std::cerr << "Invalid disk image \"" << disk_img
    //              << "\": BPB_RootEntCnt = "
    //              << this->DBR_512._BPB_.BPB_RootEntCnt << "\nAbort.\n";
    //    exit(EXIT_FAILURE);
    //}

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
        int16_t rest_zero_bytes = this->DBR_512._BPB_.BPB_BytsPerSec - 512;
        std::vector<uint8_t> datas(rest_zero_bytes);
        disk_in.read(reinterpret_cast<char *>(datas.data()), rest_zero_bytes);
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

    //  构造FAT表
    uint32_t fat16_table_bytes = static_cast<uint32_t>(this->DBR_512._BPB_.BPB_FATSz16) * static_cast<uint32_t>(this->DBR_512._BPB_.BPB_BytsPerSec);
    this->fat_table.resize(fat16_table_bytes / sizeof(FAT16_ENTRY));
    disk_in.read(reinterpret_cast<char *>(this->fat_table.data()), fat16_table_bytes);
    disk_in.seekg(static_cast<uint32_t>(this->DBR_512._BPB_.BPB_FATSz16) * static_cast<uint32_t>(this->DBR_512._BPB_.BPB_BytsPerSec), std::ios::cur); //  忽略冗余FAT

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

    //  构造根目录表
    uint32_t root_table_bytes = 32 * static_cast<uint32_t>(this->DBR_512._BPB_.BPB_RootEntCnt);
    this->root_entry_table.resize(this->DBR_512._BPB_.BPB_RootEntCnt);
    disk_in.read(reinterpret_cast<char *>(this->root_entry_table.data()), root_table_bytes);

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

    std::cout << "Disk meta data all check clear.\n"
              << "FAT table constructed.\n"
              << "Root directory constructed.\n";

    //  关闭文件流
    disk_in.close();

    //  初始当前路径为根目录 /
    this->pwd = "/";

    //  依据DBR数据构造块设备
    uint32_t total_sectors = this->DBR_512._BPB_.BPB_TotSec16 > 0 ? this->DBR_512._BPB_.BPB_TotSec16 : this->DBR_512._BPB_.BPB_TotSec32;
    this->device.reset(new FileBackedBlockDevice(disk_img, this->DBR_512._BPB_.BPB_BytsPerSec, total_sectors));

    // 检测FAT[1]并置为0x0
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
    this->fat_table[1] = 0xffff;
    // 还未完善，需要将 FAT16 的元数据写回镜像文件

    std::cout << "\"" << this->disk_name << "\" unmonted.\n";
}

bool FAT16::export_file(const std::string &src_file_path, const std::string &dest_file_path)
{
    //  清空宿主机文件的数据内容为文件导出作准备
    std::fstream fout(dest_file_path, std::ios_base::binary | std::ios_base::out);
    if (!fout.is_open())
    {
        std::cerr << "\"" << dest_file_path << "\" create failed!\n";
        return false;
    }

    // 目前不实现多级目录，略去解析路径定位子目录，核心是实现FCB分配回收与簇分配回收功能
    // 目前仅支持短文件名

    std::string filename = src_file_path;
    std::vector<DIR_ENTRY>::iterator it = this->root_entry_table.begin();
    for (; it != this->root_entry_table.end(); ++it)
    {
        //  依照目录项构造8.3格式文件名并改造为std::string
        std::string dir_filename;
        this->short_file_name_to_string(it->DIR_Name, dir_filename);

        //  按名查找
        if (dir_filename == filename)
        {
            break;
        }
    }
    if (it == this->root_entry_table.end())
    {
        std::cerr << "\"" << src_file_path << "\" does not exsit";
        return false;
    }
    else
    {
        uint16_t cluster_id = it->DIR_FstClusLO;     //    FAT16只有 DIR_FstClusLO 有用
        uint32_t remaining_bytes = it->DIR_FileSize; //    还剩多少字节要导出
        uint32_t block_id;
        std::vector<uint8_t> data_block(this->DBR_512._BPB_.BPB_BytsPerSec);

        while (cluster_id != 0xFFFF && remaining_bytes > 0) //    簇号有效且还有数据要导出
        {
            if (this->LBA_to_PA(cluster_id, block_id))
            {
                //  读取当前簇中的所有扇区，直到数据写完
                for (uint32_t i = 0; i < this->DBR_512._BPB_.BPB_SecPerClus && remaining_bytes > 0; ++i)
                {
                    this->device->read_block(block_id + i, reinterpret_cast<char *>(data_block.data()));
                    uint32_t to_write = remaining_bytes < this->DBR_512._BPB_.BPB_BytsPerSec ? remaining_bytes : this->DBR_512._BPB_.BPB_BytsPerSec;
                    fout.write(reinterpret_cast<char *>(data_block.data()), to_write);
                    remaining_bytes -= to_write;
                }

                cluster_id = this->fat_table[cluster_id]; //  沿FAT链跳到下一簇
            }
            else
            {
                std::cerr << "Invalid cluster_id = " << cluster_id << ", export failed.\n";
                return false;
            }
        }

        fout.close();
        std::cout << "Export success!\n";
        return true;
    }
}

bool FAT16::load_file(const std::string &src_file_path, const std::string &dest_file_path)
{
    //  打开宿主机源文件为导入作准备
    std::fstream fin(src_file_path, std::ios_base::binary | std::ios_base::in);
    if (!fin.is_open())
    {
        std::cerr << "\"" << src_file_path << "\" open failed!\n";
        return false;
    }

    //  计算文件大小
    fin.seekg(0, std::ios::end);
    uint64_t file_size = fin.tellg();
    fin.clear();
    fin.seekg(0, std::ios::beg);
    uint32_t cluster_size = this->DBR_512._BPB_.BPB_BytsPerSec * this->DBR_512._BPB_.BPB_SecPerClus;
    uint32_t clusters_need = (file_size + cluster_size - 1) / cluster_size;

    //  检查可用簇，并且可以直接拒绝超出文件系统限制的最大文件限制的文件
    uint64_t free_clusters = 0;
    std::for_each(this->fat_table.begin(), this->fat_table.end(), [&free_clusters](FAT16_ENTRY &a)
                  { if (a == 0x0000) ++free_clusters; });
    if (clusters_need >= free_clusters)
    {
        std::cerr << "Insufficient image capacity! File is too big!\n";
        return false;
    }

    //  目前不实现多级目录，略去解析路径定位子目录，核心是实现FCB分配回收与簇分配回收功能
    //  目前仅支持短文件名
    std::string filename = dest_file_path;

    //  先检查是否有无重名文件
    for (std::vector<DIR_ENTRY>::iterator it = this->root_entry_table.begin(); it != this->root_entry_table.end(); ++it)
    {
        std::string exist_filename;
        this->short_file_name_to_string(it->DIR_Name, exist_filename);
        if (exist_filename == filename)
        {
            std::cerr << "file \"" << dest_file_path << "\" has already existed!\n";
            return false;
        }
    }

    //  查找可用 PCB
    //  先查找被删除文件留下的目录项
    std::vector<DIR_ENTRY>::iterator free_ent_it = this->root_entry_table.begin() + 2;
    for (; free_ent_it != this->root_entry_table.end(); ++free_ent_it)
    {
        if (free_ent_it->DIR_Name[0] == 0xE5)
        {
            break;
        }
    }
    //  否则查找空闲目录项
    if (free_ent_it == this->root_entry_table.end())
    {
        free_ent_it = this->root_entry_table.begin();
        for (; free_ent_it != this->root_entry_table.end(); ++free_ent_it)
        {
            if (free_ent_it->DIR_Name[0] == 0x00)
            {
                break;
            }
        }
    }

    //  无可用 FCB
    if (free_ent_it == this->root_entry_table.end())
    {
        std::cerr << "No free FCB!\n";
        return false;
    }

    //  填写文件元数据
    this->string_to_short_file_name(filename, free_ent_it->DIR_Name); //  填写8.3格式短文件名
    free_ent_it->DIR_Attr = 0x00;                                     //  填写文件属性
    free_ent_it->DIR_NTRes = 0x00;                                    //  保留位强制置0
    //  暂时忽略文件创建时间，单位为10ms
    //  暂时忽略文件创建时间
    //  暂时忽略文件创建日期
    //  暂时忽略文件最近访问日期
    free_ent_it->DIR_FstClusHI = 0x0000; //  首簇簇号高16位在 FAT16 中始终为0
    //  暂时忽略文件修改时间
    //  暂时忽略文件修改日期
    free_ent_it->DIR_FileSize = file_size; //  先填写文件大小，单位为字节

    //  构造镜像文件数据
    // 查找第一个空闲簇
    uint32_t cluster_id = 2;
    for (; cluster_id <= this->get_total_clusters() + 1; ++cluster_id)
    {
        if (this->fat_table[cluster_id] == 0x0000)
        {
            break;
        }
    }
    if (cluster_id < 2 || cluster_id > this->get_total_clusters() + 1)
    {
        std::cerr << "FAT table error!\n";
        return false;
    }
    free_ent_it->DIR_FstClusLO = static_cast<uint16_t>(cluster_id); //  填写首簇簇号低16位，至此文件元数据填写完毕

    //  以下代码开始进行文件数据导入工作
    uint32_t remaining_bytes = free_ent_it->DIR_FileSize;
    uint32_t block_id;
    std::vector<uint8_t> data_block(this->DBR_512._BPB_.BPB_BytsPerSec);
    while (remaining_bytes > 0)
    {
        if (this->LBA_to_PA(cluster_id, block_id))
        {
            //  写入当前簇中的所有扇区，直到数据读完
            for (uint32_t i = 0; i < this->DBR_512._BPB_.BPB_SecPerClus && remaining_bytes > 0; ++i)
            {
                uint32_t to_read = remaining_bytes < this->DBR_512._BPB_.BPB_BytsPerSec ? remaining_bytes : this->DBR_512._BPB_.BPB_BytsPerSec;
                fin.read(reinterpret_cast<char *>(data_block.data()), to_read);
                this->device->write_block(block_id + i, reinterpret_cast<char *>(data_block.data()));
                remaining_bytes -= to_read;
            }

            //  找到下一个空闲簇
            if (remaining_bytes > 0)
            {
                uint32_t new_cluster_id = cluster_id + 1;
                for (; new_cluster_id <= this->get_total_clusters() + 1; ++new_cluster_id)
                {
                    if (this->fat_table[new_cluster_id] == 0x0000)
                    {
                        this->fat_table[cluster_id] = static_cast<uint16_t>(new_cluster_id); //  构造文件簇链
                        cluster_id = new_cluster_id;
                        break;
                    }
                }

                if (new_cluster_id < 2 || new_cluster_id > this->get_total_clusters() + 1)
                {
                    std::cerr << "FAT table error!\n";
                    return false;
                }
            }
            else //  若已经读入所有数据，则添加终止标记
            {
                this->fat_table[cluster_id] = 0xffff;
            }
        }
        else
        {
            std::cerr << "Invalid cluster_id = " << cluster_id << ", load failed.\n";
            return false;
        }
    }

    fin.close();
    std::cout << "Load success!\n";
    return true;
}

bool FAT16::short_file_name_to_string(const uint8_t *dir_name, std::string &filename)
{
    std::string name(reinterpret_cast<const char *>(dir_name), 8);
    name.erase(name.find_last_not_of(' ') + 1);
    std::string ext(reinterpret_cast<const char *>(dir_name + 8), 3);
    ext.erase(ext.find_last_not_of(' ') + 1);
    filename = name;
    if (!ext.empty())
    {
        filename += '.';
        filename += ext;
    }
    return true;
}

bool FAT16::string_to_short_file_name(const std::string &filename, uint8_t *dir_name)
{
    if (filename.size() > 12)
    {
        return false;
    }

    memset(dir_name, ' ', 11);

    size_t dot_pos = 0;
    for (; dot_pos < filename.size(); ++dot_pos)
    {
        if (filename[dot_pos] == '.')
        {
            break;
        }
    }
    if (dot_pos >= filename.size())
    {
        return false;
    }
    for (size_t i = 0; i < dot_pos; ++i)
    {
        dir_name[i] = filename[i];
    }
    for (size_t i = dot_pos + 1; i < filename.size(); ++i)
    {
        dir_name[i - dot_pos - 1 + 8] = filename[i];
    }

    return true;
}

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