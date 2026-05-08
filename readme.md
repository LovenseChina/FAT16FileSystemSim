# FAT16 文件系统模拟器

一个基于 **FAT16** 文件系统格式的轻量级模拟器，支持磁盘镜像的创建、读写、目录管理及文件传输。该项目使用 C++11 实现，提供了面向用户的 Shell 交互界面和底层块设备抽象，可用于学习 FAT16 内部结构或作为嵌入式文件系统的原型。

## 功能特性

- **完整的 FAT16 超级软盘格式支持**：自动识别 DBR、FAT 表、根目录与数据区。
- **文件/目录操作**：
  - 创建、删除、读取、写入文件（支持覆盖写）
  - 创建、删除空目录
  - 切换当前工作目录（`cd`）与显示当前路径（`pwd`）
- **文件传输**：
  - 从主机导入文件到镜像（`import`）
  - 从镜像导出文件到主机（`export`）
- **持久化**：所有修改可通过 `sync` 命令或程序正常退出时自动同步到底层磁盘镜像文件。
- **安全卸载**：通过 FAT[1] 脏位标记检测上次是否正常卸载，防止数据损坏。
- **命令行 Shell**：内置交互式命令行。

## 项目结构

project/
├── include/ # 头文件目录
│ ├── BlockDevice.hpp # 块设备抽象基类及文件实现类声明
│ └── FAT16.hpp # FAT16 核心类声明
├── src/ # 源文件目录
│ ├── BlockDevice.cpp # 块设备实现（文件回写）
│ ├── FAT16.cpp # FAT16 核心逻辑实现
│ └── FAT16Shell.cpp # 交互式 Shell 主程序
├── tool/ # 一些简易工具
│ ├── endian_checker.cpp # 查看机器大小端
│ ├── fat16_checker.cpp # FAT16 检查验证 FAT16 镜像有效性
│ ├── fat16_disk_generator.cpp # FAT16 空镜像生成器，固定生成约 32mb 大小的合法镜像
│ └── hexer.cpp # 用于查看文件二进制数据的简易软件
└── README.md # 本文件


## 编译与运行

### 系统要求
- C++11 兼容编译器（如 g++ 4.8+，clang 3.3+）
- 支持标准文件流（`<fstream>`）

### 编译方式

进入项目根目录（即 `include/` 与 `src/` 的父目录），执行以下命令：

```bash
g++ -std=c++11 -I include src/BlockDevice.cpp src/FAT16.cpp src/FAT16Shell.cpp -o fat16shell
```
### 说明：

-I include 指定头文件搜索路径，使 #include "../include/..." 能正确找到头文件。

编译后生成可执行文件 fat16shell。

## 运行
./fat16shell [磁盘镜像路径]

若不提供磁盘镜像路径，默认使用 FAT16.img。

如果镜像文件不存在或格式无效，程序会报错退出。您可以使用工具fat16_disk_generator（由project/tool/fat16_disk_generator.cpp 直接单文件编译即可）生成一个空镜像进行测试

## 使用说明
在 Shell 提示符 fat16:/$ 后输入命令。所有路径均遵循 POSIX 风格，支持绝对路径（以 / 开头）和相对路径（相对于当前目录）。

## 命令列表

命令	语法	描述
ls	ls [path]	列出指定目录（默认为当前目录）的内容
cd	cd <path>	更改当前工作目录
pwd	pwd	打印当前工作目录（绝对路径）
mkdir	mkdir <path>	创建新目录（路径最后一级为目录名）
rmdir	rmdir <path>	删除空目录
touch	touch <path>	创建一个空文件（大小为0）
rm	rm <path>	删除文件
cat	cat <path>	将文件内容打印到标准输出
write	write <path> <content>	将 <content> 字符串写入文件（覆盖原有内容）
import	import <host-file> <image-path>	将主机上的文件复制到磁盘镜像内
export	export <image-path> <host-file>	将镜像内的文件复制到主机上
sync	sync	手动将内存中的 FAT 表、根目录、数据区刷新到磁盘文件
help	help	显示帮助信息
exit	exit	退出 Shell（自动执行 sync）

## 示例
```bash
# 挂载镜像（假设已有 FAT16.img）
./fat16shell FAT16.img

# 创建目录并进入
fat16:/$ mkdir test
fat16:/$ cd test
fat16:/test/$ pwd
/test/

# 创建文件并写入内容
fat16:/test/$ touch a.txt
fat16:/test/$ write a.txt "Hello, FAT16!"

# 查看文件内容
fat16:/test/$ cat a.txt
Hello, FAT16!

# 导入主机文件
fat16:/test/$ import /home/user/photo.jpg photo.jpg

# 导出的镜像文件到主机
fat16:/test/$ export a.txt /tmp/a.txt

# 删除文件
fat16:/test/$ rm a.txt

# 返回根目录并删除 test（需先删除其中所有文件，本例 test 下剩余 photo.jpg）
fat16:/test/$ cd /
fat16:/$ rm test/photo.jpg
fat16:/$ rmdir test
fat16:/$ exit
Goodbye.
```

## 注意事项
1. 镜像格式限制
本模拟器仅支持 FAT16 超级软盘格式（无 MBR 分区表，卷直接始于 DBR）。如果使用其他工具创建镜像，请确保满足：

- 扇区大小 512/1024/2048/4096 字节

- 保留扇区数 > 0

- FAT 表大小 > 0

- 总簇数在 4085 ~ 65525 之间

2. 文件名规范

- 短文件名（8.3 格式）：主名最多 8 字符，扩展名最多 3 字符，字母自动转为大写。

- 不支持长文件名（LFN），Shell 会拒绝不合法的文件名（如包含空格、*?<>| 等字符）。

- 保留名 . 和 .. 不能作为用户创建的文件或目录名。

3. 目录删除
rmdir 仅能删除空目录（即除 . 和 .. 外无其他条目）。删除非空目录前需手动清空其中的文件和子目录。

4. 持久化时机

- 正常执行 exit 或程序自然结束（析构函数调用）会自动调用 sync。

- 使用 Ctrl+C 强制终止不会执行 sync，可能导致磁盘镜像损坏（下次挂载时会显示警告）。

- 建议在关键操作后手动执行 sync。

5. 块设备抽象层
FileBackedBlockDevice 将每个扇区视为一个块，使用 std::fstream 进行随机读写。所有 FAT16 上层 API 均基于簇操作，最终通过块设备写入镜像文件。

## 作者与致谢
- 作者：Tang Jung-Chi (chinatrq@outlook.com)

- FAT16Shell.cpp 代码生成：DeepSeek (https://deepseek.com) (本人已 code review)

- 设计参考：YatSunOS v2 Tutorial (https://ysos.gzti.me/)

## 许可证
本项目仅供学习交流使用，无特殊许可证声明。