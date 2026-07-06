# FAT12 文件系统工具 (`fat_tool`)

`fat_tool` 是一个采用 C 语言实现的命令行工具，用于创建、管理和操作标准 1.44MB FAT12 格式的磁盘镜像文件，支持一级子目录操作及递归删除。

## 1. 设计构思

### 文件系统规范
本工具严格遵循 **FAT12** 文件系统的标准，针对 **1.44MB 软盘** 布局进行配置。

| 参数 | 值 | 说明 |
| :--- | :--- | :--- |
| 扇区大小 | 512 Bytes | BPB_BytsPerSec |
| 总扇区数 | 2880 | BPB_TotSec16 |
| FAT 表数量 | 2 | BPB_NumFATs |
| FAT 扇区数 | 9 | BPB_FATSz16 |
| 根目录条目数 | 224 | BPB_RootEntCnt |
| 簇大小 | 1 扇区 | BPB_SecPerClus |

### 核心实现和技术难点
1.  **模块化设计**: 代码分为 I/O 层 (`fat_io`)、表处理层 (`fat_table`)、工具层 (`fat_utils`) 和应用层 (`fat_tool`)，逻辑清晰。
2.  **FAT12 12位读写逻辑**: 精确处理了跨字节边界的 12 位簇链读写（1.5 字节对齐）。
3.  **一级子目录支持**: 实现了路径解析逻辑，支持 `/DIR/FILE` 格式的路径访问。
4.  **递归删除逻辑**: 删除目录时，会递归释放目录下所有文件和子目录占用的簇空间。
5.  **健壮性保证**: 所有创建和导入操作均包含**重名检查**；删除操作包含**根目录保护**及**用户确认提示**。

## 2. 构建和编译

本项目支持模块化编译。

**先决条件**: `gcc`, `make`

**编译命令**:
```bash
make clean && make
```
编译后生成可执行文件 `fat_tool`。

## 3. 使用说明

`fat_tool` 支持以下核心命令：

### 3.1. 创建磁盘镜像
创建一个标准的 1.44MB FAT12 镜像。
```bash
./fat_tool create <image_path>
```

### 3.2. 创建子目录
在根目录下创建一级子目录。
```bash
./fat_tool mkdir <image_path> <dir_path>
# 示例: ./fat_tool mkdir floppy.img /DATA
```

### 3.3. 导入外部文件
将本地文件导入镜像的根目录或一级子目录。会自动检查重名。
```bash
./fat_tool import <image_path> <src_path> <dest_path_in_image>
# 示例: ./fat_tool import floppy.img ./readme.txt /DATA/README.TXT
```

### 3.4. 导出文件
将镜像中的文件导出到本地系统。
```bash
./fat_tool export <image_path> <src_path_in_image> <dest_path>
# 示例: ./fat_tool export floppy.img /DATA/README.TXT ./exported.txt
```

### 3.5. 列出目录内容
列出目录下的所有条目（包含 `.` 和 `..`）。支持根目录及一级子目录。
```bash
./fat_tool ls <image_path> <dir_path>
```

### 3.6. 递归删除
删除文件或目录。如果是目录，将提示确认并进行递归删除。**根目录禁止删除。**
```bash
./fat_tool rm <image_path> <target_path>
```

## 4. 相关参考文档
*   [Microsoft FAT Specification](https://web.archive.org/web/20070621213324/http://www.microsoft.com/whdc/system/platform/firmware/fatgen.mspx)
*   [OS Dev Wiki: FAT](https://wiki.osdev.org/FAT)

## 5. 原型限制
*   **路径深度**: 目前路径解析支持深度为 1 的子目录（即根目录下的直接子目录）。
*   **文件名**: 仅支持 FAT 8.3 短文件名格式，不支持长文件名 (LFN)。
*   **磁盘空间**: 未实现复杂的碎片整理；若磁盘空间不足或目录项占满（根目录 224 条，子目录单簇容量），操作将提示失败。
