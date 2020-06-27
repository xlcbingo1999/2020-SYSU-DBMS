# 2020-DBMS-project 第8组

## 实验报告

[8_proj_task2_report](8_proj_task2_report.md)

## 使用说明

### 挂载NVM

打开terminal，在root权限下输入以下指令：

* mkfs.ext4 /dev/pmem0
* mount -o dax /dev/pmem0 [你的数据地址,如本例是/home/Desktop/2020-SYSU-DBMS/data]
* df -h

### 使用gtest

进入test文件夹，然后输入

* make clean
* make

之后进入test/bin文件夹中，输入以下指令：

* ./ehash_test

### 使用ycsb测试

进入src文件加，然后输入:

* make clean
* make

之后进入src/bin文件夹中，输入以下指令：

* ./ycsb

即可完成本次实验的测试。

### sth_self

sth_self是在本次编码过程中使用的中间文件，然后test.txt是测试的结果。
