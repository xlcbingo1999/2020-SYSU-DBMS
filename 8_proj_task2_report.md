# 2020-DBMS-project  第8组实验报告

授课教师：冯剑琳                         

| 第8组成员姓名 |   学号   |               组内贡献                |
| :-----------: | :------: | :-----------------------------------: |
|    肖霖畅     | 18342105 | 实现大部分代码框架功能 & 编写makefile |
|    伍浩源     | 18342102 |  自编写ycsb读取workload进行性能测试   |
|    唐乾力     | 18342089 |  实现部分代码框架功能（insert功能）   |
|    赵文序     | 18342137 |  实现部分代码框架功能（remove功能）   |

> 注：本实验报告由组内成员共同完成，最后统一调试上传
>
> [代码仓库地址]: https://github.com/xlcbingo1999/2020-SYSU-DBMS



# INDEX

[toc]

# 一、实验目的

​	本次课程设计要求实现一个课本上所讲解的可扩展哈希的持久化实现，底层持久化是在模拟的	      	NVM硬件上面进行。

​	直观理解：扩展哈希持久化在NVM上，使用NVM编程方法进行文件操作，然后文件数据管理通过	自己设计的定长页表进行空间管理，	为哈希分配和回收空间。

## 1.1 涉及知识点

- 可扩展哈希
- 简单的NVM编程
- gtest单元测试
- 编写makefile编译
- github团队编程
- 定长页表设计和使用
- 简单的页表文件空间管理



## 1.2 项目讲解

​	项目是一个可扩展哈希，存储定长键值对形式的数据，提供给外界的接口只有对键值对的增删改查操作，底层存储与模拟NVM硬件进	行交互，将数据持久存储在文件中，重启时能够重新恢复可扩展哈希的存储状态。



# 二、实验要求

## 2.1 文件夹说明

| 文件夹名称      | 功能                                                         |
| --------------- | ------------------------------------------------------------ |
| data            | 存放可扩展哈希的相关数据页表，目录以及元数据，即存放PmEHash对象数据文件的文件夹。 |
| gtest           | Google Test源文件，不需要动                                  |
| include         | 项目相关头文件                                               |
| PMDK-dependency | PMDK相关依赖                                                 |
| src             | 项目相关源文件                                               |
| task            | 课程设计任务文档说明                                         |
| test            | 项目需要通过的测试源文件                                     |
| workload        | YCSB benchmark测试的数据集                                   |



## 2.2 项目总体步骤

1. 安装PMDK
2. 用内存模拟NVM
3. 实现代码框架的功能并进行简单的Google test，运行并通过ehash_test.cpp中的简单测试
4. 编写main函数进行YCSB benchmark测试，读取workload中的数据文件进行增删改查操作，测试运行时间并截图性能结果。



## 2.3 需要编写的文件说明

1. src下所有源文件以及makefile
2. src下自编写ycsb读取workload进行性能测试
3. 如增添源文件，修改test下的makefile进行编译
4. include中所有头文件进行自定义增添和补充



## 2.4 项目架构示意图

![Nce3eU.png](https://s1.ax1x.com/2020/06/27/Nce3eU.png)



## 2.5 项目代码实现说明

> 关注增删改查接口，恢复功能接口以及数据页的功能实现即可，其他函数和数据结构都是服务于它们的，所以项目里private的接口函数	最好能用项目规定的，不用也可以自定义自己的函数和数据结构，前提是要解释清楚功能作用。

​	具体要求如下：

1. **pm_ehash.h & pm_ehash.cpp**: 基本的增删改查接口操作以及其他要调用的函数实现
2. **data_page.h & data_page.cpp**: 底层页表设计和简单的空间管理
3. **ehash_test.cpp**: 通过gtest简单的测试
4. **ycsb.cpp**: 自编写测试代码进行YCSB测试
5. 底层NVM编程，修改数据时要符合NVM的规范
6. makefile编译命令编写，生成ycsb和ehash_test的编译命令(ehash_test我写了基本的，没有加data_page或者其他源文件的编译链接，需要自己实现后自行加入)



# 三、实验流程

## 3.1 底层设计

​	本次实验的主体部分包含在data_page.h和pm_eash.h这两个头文件当中。构建了整个底层部分，同时包含了对数据的操作。

### `data_page.h`中包含了本次实验所需的数据结构：

---

​	`bucker_inner_key :  uint64_t key, uint64_t value`

​	这个结构的功能是作为桶内部的u插入的键值，存储数据到桶里面。

---

​	`page_inner_bucket：  uint8_t bitmap[2]，bucket_inner_kv inner_kv[15] ，uint8_t unused_byte_in_inner_bucket[13]`

​	该结构功能是作为页表的内部桶去存储信息，bitmap是作为桶是否使用的标志，unused_byte_in_inner_bucket表示桶地址中的哪位没有使用。

---

​	`data_page: uint16_t bitmap, page_inner_bucket page_bucker[DATA_PAGE_SLOT_NUM], uint8_t unused_byte_in_data_page[14]`

​	该结构的功能是起到页表的作用，bitmap表示该表是否使用

---

### `pm_eash.h`中包含了数据库可执行的操作以及哈希表的相关信息：

​	`pm_address: uint32_t fileId, uint32_t offset`

​	这个结构的作用作为数据存储的地址，fileId相当于页表的编号，offset偏移量即该数据存储的地址与页表首地址的差值。

---

​	`kv: uint64_t key, uint64_t value`

​	这个结构即使哈希表中的键值

---

​	`pm_bucket: uint64_t local_depth,   uint8_t bitmap[BUCKET_SLOT_NUM / 8 + 1],  kv slot[BUCKET_SLOT_NUM];`

​	local_depth表示深度,bitmap表示槽是否使用,slot即槽，启动存放数据的作用。

---

​	`ehash_catalog: pm_address* buckets_pm_address, pm_bucket** buckets_virtual_address`

​	这个结构起到了目录的作用，buckets_pm_address存储桶的地址数组，buckets_virtual_address即为bucket_pm_address指向的桶的虚拟地址。

---

​	`ehash_metadata: uint64_t max_file_id, uint64_t catalog_size, uint64_t global_depth`

​	这个结构是元数据，max_file_id是表示下一个可用页号，catalog_size表示目录文件大小，global_depth说明全局深度。

---

​	PmEHash是本项目的主要实现部分，因为其内部成员过多，比较负载。因此直接以代码的形式展示出来。PmEHash与上面重复的部分不再重新讲述，这里只说明新部分。

```c++
class PmEHash
{
private:
	data_page **page_pointer_table;
	ehash_metadata* metadata;      // virtual address of metadata, mapping the metadata file
	ehash_catalog catalog;         // the catalog of hash
	queue<pm_bucket*> free_list;           //all free slots in data pages to store buckets
	map<pm_bucket*, pm_address> vAddr2pmAddr;    // map virtual address to pm_address, used to find specific pm_address
	map<pm_address, pm_bucket*> pmAddr2vAddr;    // map pm_address to virtual address, used to find specific virtual address
	uint64_t hashFunc(uint64_t key)；
	pm_bucket* getFreeBucket(uint64_t key);
	pm_bucket* getNewBucket();
	void freeEmptyBucket(pm_bucket* bucket);
	kv* getFreeKvSlot(pm_bucket* bucket);
	void splitBucket(uint64_t bucket_id);
	void mergeBucket(uint64_t bucket_id);
	void extendCatalog();
	void* getFreeSlot(pm_address& new_address);
	void allocNewPage();
	void recover();
	void mapAllPage();

public:
	PmEHash();
	~PmEHash();
	int insert(kv new_kv_pair);
	int remove(uint64_t key);
	int update(kv kv_pair);
	int search(uint64_t key, uint64_t& return_val);
	void selfDestory();
};

```

首先先解释下**私有成员**部分：

```c++
queue<pm_bucket*> free_list ：

//free_list是一个队列，用来存放页表中空闲的槽

map<pm_bucket*, pm_address> vAddr2pmAddr：

//vAddr2pmAddr即是一个图，映射虚拟地址到pm地址上

map<pm_address, pm_bucket*> pmAddr2vAddr

//与上面是相反的作用
```


接下来介绍**私有函数**部分，这里我不完整地展示所有代码，而是将清楚大体的操作方法：

```c++
uint64_t hashFunc(uint64_t key)：
//顾名思义哈希函数，通过输入的key找到对应的桶。具体实现见小组github
```
```c++
uint64_t bucketID = key & ((1 << metadata->global_depth) - 1); // 位操作，相当于对(1 << metadata->global_depth)求余
return bucketID;
```

​	`pm_bucket* getFreeBucket(uint64_t key)`

​	该函数用来寻找新的桶。首先通过传入的参数key来找到对应的桶号。接下来判断桶是否满了，如果桶满了，就分裂新的桶，并调用哈希函数找到新的桶号

```c++
while (catalog.buckets_virtual_address[bucketID]->bitmap[0] == 255 && catalog.buckets_virtual_address[bucketID]->bitmap[1] == 254) {
        // 如果分桶后，插入的桶仍然是满的，就需要再一次分桶
    splitBucket(bucketID);
    bucketID = hashFunc(key);
}
```

​	找到新的桶之后，将桶的虚拟地址返回。

​	`kv* getFreeKvSlot(pm_bucket* bucket);`

​	该函数用于获取新的槽插入。首先，我们将传入的参数 bucket中的bitmap赋值到一个临时变量bucket_map中，然后开始遍历槽，遇到	第一个为空的槽时，将位图的值修改，并返回一个kv。如下图所示

```c++
pm_bucket* new_bucket = free_list.front();
new_bucket->local_depth = metadata->global_depth; 
free_list.pop();
new_address = vAddr2pmAddr.find(new_bucket)->second;
return new_bucket;
```

​	`void extendCatalog()`: 

​	该函数用于扩增目录，按照要求，我们需要生成新的目录文件，并将旧的目录文件复制过去，再将旧的目录文件删除。我们首先需要将	当前目录的大小和空列表的大小记录下来，再创建一个临时的目录文件。这个临时的目录文件的作用是储存源目录文件的内容，之后删	除掉原来的buckets_pm_address和buckets_virtual_address，在原来的目录文件上进行再分配空间，空间大小为原来大小的两倍，并	把原来的内容从临时目录文件中移植到新的buckets_pm_address和buckets_virtual_address中。

```c++
void PmEHash::extendCatalog()
{
    uint64_t ori_cata_size = metadata->catalog_size;
    int ori_cata_file_num;
    if(ori_cata_size < 512){
        ori_cata_file_num = 1;
    } else {
        ori_cata_file_num = ori_cata_size / 512;
    }
    uint64_t free_bucket_size = free_list.size();
    ehash_catalog temp_catalog;
    temp_catalog.buckets_pm_address = new pm_address[ori_cata_size];
    temp_catalog.buckets_virtual_address = new pm_bucket*[ori_cata_size];
    catalog_page_file* temp_catalog_file_table[ori_cata_file_num];
    for (int i = 0; i < ori_cata_size; ++i) {
        temp_catalog.buckets_pm_address[i] = catalog.buckets_pm_address[i];
        temp_catalog.buckets_virtual_address[i] = catalog.buckets_virtual_address[i];
    }
    for(int i = 0; i < ori_cata_file_num; ++i) {
        temp_catalog_file_table[i] = catalog_file_table[i];
    }
    catalog.buckets_pm_address = new pm_address[ori_cata_size * 2];
    catalog.buckets_virtual_address = new pm_bucket*[ori_cata_size * 2];
    
    size_t map_len;
    int is_pmem;
    if(ori_cata_size >= 512){
        catalog_file_table = new catalog_page_file*[ori_cata_file_num * 2];
    }

    for (int i = 0; i < ori_cata_size; ++i) {
        catalog.buckets_pm_address[i] = temp_catalog.buckets_pm_address[i];
        catalog.buckets_virtual_address[i] = temp_catalog.buckets_virtual_address[i];
        catalog.buckets_pm_address[ori_cata_size + i] = temp_catalog.buckets_pm_address[i];
        catalog.buckets_virtual_address[ori_cata_size + i] = temp_catalog.buckets_virtual_address[i];
    }

    const char* data_dir = PM_EHASH_DIRECTORY;
    const char* catalog_file_dir = CATALOG_NAME;
    int size_of_catalog_dir = strlen(data_dir) + strlen(catalog_file_dir) + 1;
    int count = 0;
    char* temp_catalog_dir;
    temp_catalog_dir = new char[size_of_catalog_dir];
    for (int i = 0; i < strlen(data_dir); ++i) {
        temp_catalog_dir[count++] = data_dir[i];
    }
    for (int i = 0; i < strlen(catalog_file_dir); ++i) {
        temp_catalog_dir[count++] = catalog_file_dir[i];
    }
    temp_catalog_dir[count] = '\0';
    std::string fin_catalog_dir = "";
    for(int i = 0; i < count; ++i){
        fin_catalog_dir += temp_catalog_dir[i];
    }
    std::string temp_fin_catalog_dir = fin_catalog_dir;
    for (int i = 0; i < ori_cata_size; ++i) {
        if(i % 512 == 0){
            std::string num2string = std::to_string(i/512);
            fin_catalog_dir = temp_fin_catalog_dir;
            fin_catalog_dir += num2string;
            const char* final_catalog_dir = fin_catalog_dir.c_str();
            catalog_file_table[i/512] = (catalog_page_file*)pmem_map_file(final_catalog_dir, sizeof(catalog_page_file), PMEM_FILE_CREATE, 0777, &map_len, &is_pmem);
            // catalog_file_table[i/512] = temp_catalog_file_table[i/512];
        } 
        catalog_file_table[i/512]->catalog_item[i%512].fileId = catalog.buckets_pm_address[i].fileId;
        catalog_file_table[i/512]->catalog_item[i%512].offset = catalog.buckets_pm_address[i].offset;
        if((i + ori_cata_size) % 512 == 0){
            std::string num2string = std::to_string((i + ori_cata_size) / 512);
            fin_catalog_dir = temp_fin_catalog_dir;
            fin_catalog_dir += num2string;
            const char* final_catalog_dir = fin_catalog_dir.c_str();
            catalog_file_table[(i + ori_cata_size) / 512] = (catalog_page_file*)pmem_map_file(final_catalog_dir, sizeof(catalog_page_file), PMEM_FILE_CREATE, 0777, &map_len, &is_pmem);
            // catalog_file_table[(i + ori_cata_size) / 512] = temp_catalog_file_table[i];
        }
        catalog_file_table[(i + ori_cata_size)/512]->catalog_item[(i + ori_cata_size)%512].fileId = catalog.buckets_pm_address[i+ori_cata_size].fileId;
        catalog_file_table[(i + ori_cata_size)/512]->catalog_item[(i + ori_cata_size)%512].offset = catalog.buckets_pm_address[i+ori_cata_size].offset;
    }
    metadata->catalog_size = ori_cata_size * 2;
    metadata->global_depth += 1;
}

```

​	`void allocNewPage()`:

​	该函数用于分配新的页表。首先指定文件名和文件路径，创建变量size_of_dir，其值相当于文件名和文件路径的字符串长度之和加一，	之后建立一个字符数组final_dir，大小为size_of_dir，并将文件名和文件路径赋值进去。然后从metadata中获得下一页的页号，将页号	和fianl_dir在加入进	新的字符数组中。接下来开始调用pmem_map_file函数去构建一个新的页new_page，类型为data_page，并将	new_page中的bitmap设置为0。然后对新的页进行初始化操作，这个初始化操作还包括对页表中的新建的桶和槽进行初始化操作，在	初始化操作完成后，将页表的pm_address和pm_bucket插入到vAddr2pmAddr和 pmAddr2vAddr中。

```c++
void PmEHash::allocNewPage()
{
    const char* data_dir = PM_EHASH_DIRECTORY;
    const char* data_file = FILE_NAME;
    char* final_dir;
    int size_of_dir = strlen(data_dir) + strlen(data_file) + 1;
    final_dir = new char[size_of_dir];
    int count = 0;
    for (int i = 0; i < strlen(data_dir); ++i) {
        final_dir[count++] = data_dir[i];
    }
    for (int i = 0; i < strlen(data_file); ++i) {
        final_dir[count++] = data_file[i];
    }
    std::string file_name = "";
    final_dir[count] = '\0';
    for (int i = 0; i < size_of_dir - 1; ++i) {
        file_name += final_dir[i];
    }
    uint64_t name_id = metadata->max_file_id;
    std::string name_id_str = std::to_string(name_id);
    file_name += name_id_str;
    const char* file_name_c = file_name.c_str();
    size_t map_len;
    int is_pmem;
    // 新建页表文件并初始化
    data_page* new_page = (data_page*)pmem_map_file(file_name_c, sizeof(data_page), PMEM_FILE_CREATE, 0777, &map_len, &is_pmem);
    new_page->bitmap = 0;
    for (int i = 0; i < 13; ++i) {
        new_page->unused_byte_in_data_page[i] = 0;
    }
    data_page* temp_page_table[name_id];
    for (int i = 0; i < name_id; ++i) {
        temp_page_table[i] = page_pointer_table[i];
    }
    page_pointer_table = new data_page*[name_id + 1];
    for (int i = 0; i < name_id; ++i) {
        page_pointer_table[i] = temp_page_table[i];
    }
    page_pointer_table[name_id] = new_page;
    struct pm_bucket** new_bucket = new pm_bucket*[16];
    for (int j = 0; j < 16; ++j) {
        new_bucket[j] = new pm_bucket;

        new_bucket[j]->bitmap[0] = 0;
        new_bucket[j]->bitmap[1] = 0;
        for (int i = 0; i < BUCKET_SLOT_NUM; ++i) {
            new_bucket[j]->slot[i].key = 0;
            new_bucket[j]->slot[i].value = 0;
        }
        free_list.push(new_bucket[j]);
    }
    struct pm_address new_address[16];
    for (int j = 0; j < 16; ++j) {
        new_address[j].fileId = metadata->max_file_id;
        new_address[j].offset = 2 + j * 255;
        vAddr2pmAddr.insert(std::make_pair(new_bucket[j], new_address[j]));
        pmAddr2vAddr.insert(std::make_pair(new_address[j], new_bucket[j]));
    }
    metadata->max_file_id += 1;
    pmem_persist(metadata, sizeof(ehash_metadata));
}
```

（初始化操作）

​	`void* getFreeSlot(pm_address& new_address)`:

​	该函数用于获取新的槽位。先判断free_list中是否具有空闲的槽，如果有，就从free_list中弹出一个槽来使用，并使用vAddr2pmAddr的	自带的函数find，参数为从free_list中弹出的pm_bucket,找到这个pm_bucket对应的pm_address，即物理地址，并将物理地址赋值给new_address。

```c++
void* PmEHash::getFreeSlot(pm_address& new_address)
{
    if (free_list.empty()) {
        allocNewPage();
    }
    pm_bucket* new_bucket = free_list.front();
    new_bucket->local_depth = metadata->global_depth; // maybe problem.
    uint32_t fid = vAddr2pmAddr.find(new_bucket)->second.fileId;
    uint32_t off = vAddr2pmAddr.find(new_bucket)->second.offset;
    uint32_t index_bitmap = (off - 2) / 255;
    page_pointer_table[fid]->bitmap ^= (page_pointer_table[fid]->bitmap & (1 << (15 - index_bitmap)) ^ (1 << (15 - index_bitmap)));
    free_list.pop();
    new_address = vAddr2pmAddr.find(new_bucket)->second;
    return new_bucket;
}
```

​	`void splitBucket(uint64_t bucket_id)`:

​	该函数用于分裂桶。首先，从buckets_virtual_address中使用bucket_id取出对应桶的地址，赋值给个新变量ori_bucket,然后新建槽一个	键值数组kv* ori_kv。将桶ori_bucket中的槽存储的数据赋值到ori_kv中。并将ori_bucket的位图置0。接下来计算原目录对应的索引，并	进行比较，索引对应的桶地址与ori_bucket不同，则进行目录的扩增。

```C++
uint64_t mask = (1 << ori_bucket->local_depth);
uint64_t to_bucket_id = (bucket_id & (~mask)) | (bucket_id ^ mask);
if (catalog.buckets_virtual_address[to_bucket_id] != ori_bucket) {
    extendCatalog(); 
}
```

​	继续新建一个新的桶new_bucket，并使用getFreeslot获得新桶的地址new_pm_address，然后再原用ori_bucket上给它的深度加一，并	将这个深度同时赋给new_bucket。接下来将这个new_bucket加入到目录中的虚拟地址数组中。

```C++
uint64_t yu = to_bucket_id % (1 << ori_bucket->local_depth);
for (int i = 0; i < metadata->catalog_size; ++i) {
    if ((i % (1 << ori_bucket->local_depth)) == yu) {
        catalog_file_table[i/512]->catalog_item[i%512].fileId = new_pm_address.fileId;
        catalog_file_table[i/512]->catalog_item[i%512].offset = new_pm_address.offset;
    }
}
```

​	将ori_kv的值赋值到一个临时变量temp_kv中，将新地址也加入到buckets_pm_address中

```C++
uint64_t to_insert_bucketid;
kv* temp_kv;
for (int i = 0; i < BUCKET_SLOT_NUM; ++i) {
    to_insert_bucketid = hashFunc(ori_kv[i].key);
    temp_kv = getFreeKvSlot(catalog.buckets_virtual_address[to_insert_bucketid]);
    temp_kv->key = ori_kv[i].key;
    temp_kv->value = ori_kv[i].value;
}
catalog.buckets_pm_address[to_bucket_id].fileId = new_pm_address.fileId;
catalog.buckets_pm_address[to_bucket_id].offset = new_pm_address.offset;
```

​	根据新地址的fileId找到对应的页表，并将页表中的桶的位图与新桶new_bucket的位图等值，说明这些页表中的桶的状态与新桶绑定。	再将页表中的桶的内部键值赋值。并使用pmem_persist函数保存到内存上面。下半部分也同理操作。

```c++
uint32_t new_fid = new_pm_address.fileId;
int index_bitmap = (new_pm_address.offset - 2) / 255;
page_pointer_table[new_fid]->bitmap ^= (page_pointer_table[new_fid]->bitmap & (1 << (15 - index_bitmap)) ^ (1 << (15 - index_bitmap)));
page_pointer_table[new_fid]->page_bucket[index_bitmap].bitmap[0] = new_bucket->bitmap[0];
page_pointer_table[new_fid]->page_bucket[index_bitmap].bitmap[1] = new_bucket->bitmap[1];
for (int i = 0; i < BUCKET_SLOT_NUM; ++i) {
    page_pointer_table[new_fid]->page_bucket[index_bitmap].inner_kv[i].key = new_bucket->slot[i].key;
    page_pointer_table[new_fid]->page_bucket[index_bitmap].inner_kv[i].value = new_bucket->slot[i].value;
}
pmem_persist(page_pointer_table[new_fid], sizeof(data_page));

pm_address ori_pm_address = vAddr2pmAddr.find(ori_bucket)->second;
uint32_t ori_fid = ori_pm_address.fileId;
index_bitmap = (ori_pm_address.offset - 2) / 255;
page_pointer_table[ori_fid]->bitmap ^= (page_pointer_table[ori_fid]->bitmap & (1 << (15 - index_bitmap)) ^ (1 << (15 - index_bitmap)));
page_pointer_table[ori_fid]->page_bucket[index_bitmap].bitmap[0] = ori_bucket->bitmap[0];
page_pointer_table[ori_fid]->page_bucket[index_bitmap].bitmap[1] = ori_bucket->bitmap[1];
for (int i = 0; i < BUCKET_SLOT_NUM; ++i) {
    page_pointer_table[ori_fid]->page_bucket[index_bitmap].inner_kv[i].key = ori_bucket->slot[i].key;
    page_pointer_table[ori_fid]->page_bucket[index_bitmap].inner_kv[i].value = ori_bucket->slot[i].value;
}
pmem_persist(page_pointer_table[ori_fid], sizeof(data_page));
```

`void mergeBucket(uint64_t bucket_id)`:

​	该函数的作用是合并桶。首先找出需合并桶的id，再将桶的内容赋值给被归并桶，然后将空桶压入free_list中。

```c++
uint64_t mask = (1 << catalog.buckets_virtual_address[bucket_id]->local_depth);
uint64_t to_bucket_id = (bucket_id & (~mask)) | (bucket_id ^ mask);
catalog.buckets_virtual_address[to_bucket_id] = catalog.buckets_virtual_address[bucket_id];
free_list.push(catalog.buckets_virtual_address[bucket_id]);
catalog.buckets_virtual_address[bucket_id]->local_depth -= 1;
```

​	接下来在buckets_virtual_address中找桶id，满要求是该桶的深度大于或等于全局深度，找到了对应的id后将该id与目录大小进行比较，	如果id>=目录大小并且还大于2，则进行合并桶的操作。具体操作如下表示

```C++
if (i == metadata->catalog_size && i > 2) {
    uint64_t ori_cata_size = metadata->catalog_size;
    ehash_catalog temp_catalog;
    temp_catalog.buckets_pm_address = new pm_address[ori_cata_size / 2];
    temp_catalog.buckets_virtual_address = new pm_bucket*[ori_cata_size / 2];
    for (int j = 0; j < ori_cata_size / 2; ++j) {
        temp_catalog.buckets_pm_address[j] = catalog.buckets_pm_address[j];
        temp_catalog.buckets_virtual_address[j] = catalog.buckets_virtual_address[j];
    }
    catalog.buckets_pm_address = new pm_address[ori_cata_size / 2];
    catalog.buckets_virtual_address = new pm_bucket*[ori_cata_size / 2];
    for (int j = 0; j < ori_cata_size / 2; ++j) {
        catalog.buckets_pm_address[j] = temp_catalog.buckets_pm_address[j];
        catalog.buckets_virtual_address[j] = temp_catalog.buckets_virtual_address[j];
    }
    metadata->catalog_size = ori_cata_size / 2;
    metadata->global_depth -= 1;
}
```

`void recover();`

​	该函数实现了读取旧数据文件重新载入哈希，恢复哈希关闭前的状态

```c++
void PmEHash::recover()
{
    const char* data_dir = PM_EHASH_DIRECTORY;
    const char* meta_file = META_NAME;
    char* final_dir;
    int size_of_dir = strlen(data_dir) + strlen(meta_file) + 1;
    final_dir = new char[size_of_dir];
    int count = 0;
    for (int i = 0; i < strlen(data_dir); ++i) {
        final_dir[count++] = data_dir[i];
    }
    for (int i = 0; i < strlen(meta_file); ++i) {
        final_dir[count++] = meta_file[i];
    }
    final_dir[count] = '\0';
    std::fstream file_d;
    char catalog_size_temp[8];
    char max_file_id_temp[8];
    char global_depth_temp[8];
    uint64_t catalog_size_num = 0;
    uint64_t max_file_id_num = 0;
    uint64_t global_depth_num = 0;
    file_d.open(final_dir, std::ios::in | std::ios::out | std::ios::binary);
    if (file_d.is_open()) {
        for (int k = 0; k < 8; ++k) {
            file_d >> max_file_id_temp[k];
        }
        for (int k = 7; k >= 0; --k) {
            max_file_id_num = max_file_id_num * 128 + max_file_id_temp[k];
        }
        std::cout << "max_file_id_num:" << max_file_id_num << std::endl;

        for (int k = 0; k < 8; ++k) {
            file_d >> catalog_size_temp[k];
        }
        for (int k = 7; k >= 0; --k) {
            catalog_size_num = catalog_size_num * 128 + catalog_size_temp[k];
        }
        std::cout << "catalog_size_num" << catalog_size_num << std::endl;
        
        for (int k = 0; k < 8; ++k) {
            file_d >> global_depth_temp[k];
        }
        for (int k = 7; k >= 0; --k) {
            global_depth_num = global_depth_num * 128 + global_depth_temp[k];
        }
        std::cout << "global_depth_num" << global_depth_num << std::endl;
        file_d.close();
    }
    size_t map_len;
    int is_pmem;
    metadata = (ehash_metadata*)pmem_map_file(final_dir, sizeof(ehash_metadata), PMEM_FILE_CREATE, 0777, &map_len, &is_pmem);
    metadata->catalog_size = catalog_size_num;
    metadata->max_file_id = max_file_id_num;
    metadata->global_depth = global_depth_num;
    pmem_persist(metadata,sizeof(ehash_metadata));
    mapAllPage();
}
```

`void mapAllPage()`

重启时，将所有数据页进行内存映射，设置地址间的映射关系，空闲的和使用的槽位都需要设置

```c++
void PmEHash::mapAllPage()
{
    const char* data_dir = PM_EHASH_DIRECTORY;
    const char* data_file_name = FILE_NAME;
    int size_of_data_file_dir = strlen(data_dir) + strlen(data_file_name) + 1;
    int strcount = 0;
    char *temp_data_file_dir = new char[size_of_data_file_dir];
    for(int i = 0; i < strlen(data_dir); ++i){
        temp_data_file_dir[strcount++] = data_dir[i];
    }
    for(int i = 0; i < strlen(data_file_name); ++i){
        temp_data_file_dir[strcount++] = data_file_name[i];
    }
    temp_data_file_dir[strcount] = '\0';
    std::string data_file_head_dir = "";
    for(int i = 0; i < strcount; ++i){
        data_file_head_dir += temp_data_file_dir[i];
    }
    std::string temp_data_file_head_dir = data_file_head_dir;
    page_pointer_table = new data_page*[metadata->max_file_id];
    data_page** page_pointer_table_temp;
    page_pointer_table_temp = new data_page*[metadata->max_file_id];
    for(int i = 0; i < metadata->max_file_id; ++i){
        page_pointer_table_temp[i] = new data_page;
    }
    std::fstream file_d;
    uint8_t temp_bitmap[2];
    uint8_t temp_slot_bitmap[2];
    uint8_t temp_key[8];
    uint8_t temp_value[8];
    uint8_t temp_unable;
    uint16_t bitmap_get = 0;
    uint8_t slotbitmap_get[2] = {0 , 0};
    uint64_t key_get = 0;
    uint64_t value_get = 0;
    for(int i = 0; i < metadata->max_file_id; ++i){
        data_file_head_dir = temp_data_file_head_dir;
        std::string num2string = std::to_string(i);
        data_file_head_dir += num2string;
        const char* data_file_head_dir_cstr = data_file_head_dir.c_str();
        if(file_d.is_open()) file_d.close();
        file_d.open(data_file_head_dir_cstr, std::ios::in | std::ios::out | std::ios::binary);
        bitmap_get = 0;
        for(int j = 0; j < 2; ++j) {
            file_d >> temp_bitmap[j];
        }
        for(int j = 1; j >= 0; --j) {
            bitmap_get = bitmap_get * 128 + temp_bitmap[j];
        }
        page_pointer_table_temp[i]->bitmap = bitmap_get;

        for(int j = 0; j < 6; ++j) {
            file_d >> temp_unable;
        }
        for(int slot_in_page = 0; slot_in_page < DATA_PAGE_SLOT_NUM; ++slot_in_page){
            for(int j = 0; j < 2; ++j) {
                file_d >> temp_slot_bitmap[j];
            }
            for(int j = 0; j < 2; ++j) {
                page_pointer_table_temp[i]->page_bucket[slot_in_page].bitmap[j] = temp_slot_bitmap[j];
            }
            for(int j = 0; j < 6; ++j) {
                file_d >> temp_unable;
            }
            for(int slot_in_bucket = 0; slot_in_bucket < BUCKET_SLOT_NUM; ++slot_in_bucket) {
                key_get = 0;
                value_get = 0;
                for(int j = 0; j < 8; ++j){
                    file_d >> temp_key[j];
                }
                for(int j = 7; j >= 0; --j){
                    key_get = key_get * 128 + temp_key[j];
                }
                page_pointer_table_temp[i]->page_bucket[slot_in_page].inner_kv[slot_in_bucket].key = key_get;

                for(int j = 0; j < 8; ++j){
                    file_d >> temp_value[j];
                }
                for(int j = 7; j >= 0; --j){
                    value_get = value_get * 128 + temp_value[j];
                }
                page_pointer_table_temp[i]->page_bucket[slot_in_page].inner_kv[slot_in_bucket].value = value_get;
            }
            for(int j = 0; j < 16; ++j) {
                file_d >> temp_unable;
            }
        }
    }
    file_d.close();
    size_t map_len;
    int is_pmem;
    for(int i = 0; i < metadata->max_file_id; ++i){
        data_file_head_dir = temp_data_file_head_dir;
        std::string num2string = std::to_string(i);
        data_file_head_dir += num2string;
        const char* data_file_head_dir_cstr = data_file_head_dir.c_str();
        page_pointer_table[i] = (data_page*)pmem_map_file(data_file_head_dir_cstr,sizeof(data_page), PMEM_FILE_CREATE, 0777, &map_len, &is_pmem);
        page_pointer_table[i]->bitmap = page_pointer_table_temp[i]->bitmap;
        for(int j = 0; j < DATA_PAGE_SLOT_NUM; ++j){
            for(int k = 0; k < 2; ++k) page_pointer_table[i]->page_bucket[j].bitmap[k] =page_pointer_table_temp[i]->page_bucket[j].bitmap[k];
            for(int k = 0; k < BUCKET_SLOT_NUM; ++k) {
                page_pointer_table[i]->page_bucket[j].inner_kv[k].key =page_pointer_table_temp[i]->page_bucket[j].inner_kv[k].key ;
                page_pointer_table[i]->page_bucket[j].inner_kv[k].value =page_pointer_table_temp[i]->page_bucket[j].inner_kv[k].value ;
            }
        }
    }

    const char* catalog_file_dir = CATALOG_NAME;
    int size_of_catalog_dir = strlen(data_dir) + strlen(catalog_file_dir) + 1;
    int count = 0;
    char* temp_catalog_dir;
    temp_catalog_dir = new char[size_of_catalog_dir];
    for (int i = 0; i < strlen(data_dir); ++i) {
        temp_catalog_dir[count++] = data_dir[i];
    }
    for (int i = 0; i < strlen(catalog_file_dir); ++i) {
        temp_catalog_dir[count++] = catalog_file_dir[i];
    }
    temp_catalog_dir[count] = '\0';
    std::string catalog_head_dir = "";
    for(int i = 0; i < count; ++i){
        catalog_head_dir += temp_catalog_dir[i];
    }
    std::string temp_catalog_head_dir = catalog_head_dir;
    char fid_temp[4];
    char off_temp[4];
    uint32_t fid_get = 0;
    uint32_t off_get = 0;
    uint64_t catalog_page_file_num = (metadata->catalog_size % 512 == 0) ? (metadata->catalog_size / 512) : (metadata->catalog_size % 512 + 1);
    catalog_file_table = new catalog_page_file*[catalog_page_file_num];
    catalog.buckets_virtual_address = new pm_bucket*[metadata->catalog_size];
    catalog.buckets_pm_address = new pm_address[metadata->catalog_size];
    std::map<pm_address,uint32_t> address_table;
    for(int i = 0; i < metadata->catalog_size; ++i){
        if(i % 512 == 0){
            catalog_head_dir = temp_catalog_head_dir;
            std::string num2string = std::to_string(i / 512);
            catalog_head_dir += num2string;
            const char* catalog_head_dir_cstr = catalog_head_dir.c_str();
            if(file_d.is_open()) file_d.close();
            file_d.open(catalog_head_dir_cstr, std::ios::in | std::ios::out | std::ios::binary);
        }
        fid_get = 0;
        off_get = 0;
        for(int j = 0; j < 4; ++j){
            file_d >> fid_temp[j];
        }
        for(int j = 3; j >= 0; --j){
            fid_get = fid_get * 128 + fid_temp[j];
        }
        catalog.buckets_pm_address[i].fileId = fid_get;
        for(int j = 0; j < 4; ++j){
            file_d >> off_temp[j];
        }
        for(int j = 3; j >= 0; --j){
            off_get = off_get * 128 + off_temp[j];
        }
        catalog.buckets_pm_address[i].offset = off_get;
        pm_address new_address;
        new_address.fileId = fid_get;
        new_address.offset = off_get;
        std::map<pm_address,uint32_t>::iterator iter;
        for(iter = address_table.begin(); iter != address_table.end(); ++iter){
            if(iter->first.fileId == new_address.fileId && iter->first.offset == new_address.fileId) {
                address_table[new_address] += 1;
                break;
            }
        }
        if(iter == address_table.end()){
            address_table.insert(std::make_pair(new_address,1));
        }
    }
    file_d.close();
    std::map<pm_address,uint32_t>::iterator iter;
    for(iter = address_table.begin(); iter != address_table.end(); ++iter){
        pm_bucket* new_bucket = new pm_bucket;
        new_bucket->local_depth =  (metadata->global_depth >> iter->second);
        new_bucket->bitmap[0] = page_pointer_table[iter->first.fileId]->page_bucket[(iter->first.offset-2)/255].bitmap[0];
        new_bucket->bitmap[1] = page_pointer_table[iter->first.fileId]->page_bucket[(iter->first.offset-2)/255].bitmap[1];
        for(int j = 0; j < BUCKET_SLOT_NUM; ++j){
            new_bucket->slot[j].key = page_pointer_table[iter->first.fileId]->page_bucket[(iter->first.offset-2)/255].inner_kv[j].key;
            new_bucket->slot[j].value = page_pointer_table[iter->first.fileId]->page_bucket[(iter->first.offset-2)/255].inner_kv[j].value;
        }
        vAddr2pmAddr.insert(std::make_pair(new_bucket,iter->first));
        pmAddr2vAddr.insert(std::make_pair(iter->first,new_bucket));
        for(int j = 0; j < metadata->catalog_size; ++j){
            if(catalog.buckets_pm_address[j].fileId == iter->first.fileId && catalog.buckets_pm_address[j].offset == iter->first.offset){
                catalog.buckets_virtual_address[j] = new_bucket;
            }
        }
    }
}
```

`void selfDestroy()`

​	删除PmEHash对象所有数据页，目录和元数据文件，主要供gtest使用。即清空所有可扩展哈希的文件数据，不止是内存上的:

```C++
void PmEHash::selfDestory()
{
    const char* data_dir = PM_EHASH_DIRECTORY;
    const char* data_file = FILE_NAME;
    char* final_dir;
    int size_of_dir = strlen(data_dir) + strlen(data_file) + 1;
    final_dir = new char[size_of_dir];
    int count = 0;
    for (int i = 0; i < strlen(data_dir); ++i) {
        final_dir[count++] = data_dir[i];
    }
    for (int i = 0; i < strlen(data_file); ++i) {
        final_dir[count++] = data_file[i];
    }
    std::string temp_file_name = "";
    std::string file_name;
    final_dir[count] = '\0';
    for (int i = 0; i < size_of_dir - 1; ++i) {
        temp_file_name += final_dir[i];
    }
    uint64_t name_id = metadata->max_file_id;
    for (uint64_t i = 0; i < name_id; ++i) {
        file_name = temp_file_name;
        std::string name_id_str = std::to_string(i);
        file_name += name_id_str;
        const char* file_name_c = file_name.c_str();
        // 删除data目录中的数据文件
        std::remove(file_name_c);
    }

    const char* meta_file = META_NAME;
    size_of_dir = strlen(data_dir) + strlen(meta_file) + 1;
    final_dir = new char[size_of_dir];
    count = 0;
    for (int i = 0; i < strlen(data_dir); ++i) {
        final_dir[count++] = data_dir[i];
    }
    for (int i = 0; i < strlen(meta_file); ++i) {
        final_dir[count++] = meta_file[i];
    }
    final_dir[count] = '\0';
    // 删除data目录中的meta_data文件
    std::remove(final_dir);
    for (uint64_t i = 0; i < name_id; ++i) {
        page_pointer_table[i] = NULL;
    }

    const char* cata_file = CATALOG_NAME;
    size_of_dir = strlen(data_dir) + strlen(cata_file) + 1;
    final_dir = new char[size_of_dir];
    count = 0;
    for (int i = 0; i < strlen(data_dir); ++i) {
        final_dir[count++] = data_dir[i];
    }
    for (int i = 0; i < strlen(cata_file); ++i) {
        final_dir[count++] = cata_file[i];
    }
    final_dir[count] = '\0';
    std::string dir_string = "";
    for(int i = 0; i < size_of_dir - 1; ++i){
        dir_string += final_dir[i];
    }
    name_id = ((metadata->catalog_size / 512) == 0) ? 1 : (metadata->catalog_size / 512);
    for (uint64_t i = 0; i < name_id; ++i) {
        file_name = dir_string;
        std::string name_id_str = std::to_string(i);
        file_name += name_id_str;
        const char* file_name_c = file_name.c_str();
        // 删除data目录中的catalog文件
        std::remove(file_name_c);
    }
    
    for (uint64_t i = 0; i < name_id; ++i) {
        catalog_file_table[i] = NULL;
    }

    // 将目录清空
    for (int i = 0; i < metadata->catalog_size; ++i) {
        catalog.buckets_pm_address[i].fileId = 0;
        catalog.buckets_pm_address[i].offset = 0;
        catalog.buckets_virtual_address[i]->bitmap[0] = 0;
        catalog.buckets_virtual_address[i]->bitmap[1] = 0;
        catalog.buckets_virtual_address[i]->local_depth = 0;
        for (int j = 0; j < BUCKET_SLOT_NUM; ++j) {
            catalog.buckets_virtual_address[i]->slot[j].key = 0;
            catalog.buckets_virtual_address[i]->slot[j].value = 0;
        }
        catalog.buckets_virtual_address[i] = NULL;
    }
    // 清空两个map
    vAddr2pmAddr.clear();
    pmAddr2vAddr.clear();
    // 清空free_list
    int siz = free_list.size();
    for (int i = 0; i < siz; ++i) {
        free_list.pop();
    }
    // 清空所有metadata数据
    metadata->global_depth = 0;
    metadata->max_file_id = 0;
    metadata->catalog_size = 0;
}
```



接下来是pulic公有部分里面的函数，这里不将代码的所有实现都完整展示出来，而是讲清楚大致实现：

search 函数：`int search(uint64_t key, uint64_t& return_val)`

我们需要先完成search函数，无论是插入函数insert，还是删除函数remove，更新函数update，都需要先找到对应的桶或键值对才可以进行，所以search函数是本次项目中需要先完成的部分。

首先使用哈希函数以传进来的key为参数找到对应的桶号，在目录catalog下的buckets_virtual_address数组通过桶号找到位图bitmap ，bitmap是一个长度为2的数组，第一个数组元素用来寻找桶中的前八个槽，第二个数组元素用来寻找桶中的后七个槽。接下来开始遍历桶中的槽，如果槽中的key等于传入的参数key，那么就把这个key对应的value赋值给返回值return_val。如果所有的槽都没有找到对应的value，那么就返回一个-1。

`int insert(kv new_kv_pair)`:

插入函数的实现依赖于之前的函数，我们需要先调用search函数找到对应的桶和槽，如果找到了在槽里面找到了相同的key，则无法插入，返回-1。

```C++
if (search(new_kv_pair.key, returnSearchValue) == 0)
    return -1;
```

如果没有，则调用getFreeBucket函数去获得新桶toInsertBucket，再调用getFreekvslot函数获得新的键值freePlace，之后将new_kv_pari的值传给freePlace，此时调用vAddr2pmAddr.find()函数，传入参数toInsertBucket，找到对应的地址 persist_address，新建变量fid = persist_address.fileId。

接下来的操作与spiltBucket一样，将对应页表的桶的位图与当前要插入桶的位图等值，并将槽中的值传入页表桶的键值中，并使用pmem_persist函数将页表放到内存上

```c++
int index_bitmap = (persist_address.offset - 2) / 255;
page_pointer_table[fid]->page_bucket[index_bitmap].bitmap[0] = toInsertBucket->bitmap[0];
page_pointer_table[fid]->page_bucket[index_bitmap].bitmap[1] = toInsertBucket->bitmap[1];
for (int i = 0; i < BUCKET_SLOT_NUM; ++i) {
    page_pointer_table[fid]->page_bucket[index_bitmap].inner_kv[i].key = toInsertBucket->slot[i].key;
    page_pointer_table[fid]->page_bucket[index_bitmap].inner_kv[i].value = toInsertBucket->slot[i].value;
}
pmem_persist(page_pointer_table[fid], sizeof(data_page)); // 每次insert操作之后都会进行persist
return 0;
```

`int remove(uint64_t key)`:

该函数起到移除数据的作用。同插入函数一样，先查找对应的桶和槽，如果search的返回值为-1，即桶中不存在这样键key，那自然无法删除，就返回-1。如果存在，则使用哈希函数找到对应的桶号，并利用桶号从虚拟地址表中找到页表号

```C++
uint64_t bucketID = hashFunc(key);
uint8_t temp = 128;
uint32_t fid = vAddr2pmAddr.find(catalog.buckets_virtual_address[bucketID])->second.fileId;
```

有了页表号和桶号后，就去找到桶的槽，遍历15个槽，如果找到了对应的key，就对应的槽的位图和表中的桶的位图置0，表示清除。然后返回0，表示清除成功。以下是前八个桶的遍历:

```C++
for (int i = 0; i < 8; ++i) {
    if ((catalog.buckets_virtual_address[bucketID]->bitmap[0] & temp) != 0 && catalog.buckets_virtual_address[bucketID]->slot[i].key == key) {
        catalog.buckets_virtual_address[bucketID]->bitmap[0] &= (~(1 << (7 - i)));
        page_pointer_table[fid]->page_bucket->bitmap[0] &= (~(1 << (7 - i)));
        pmem_persist(page_pointer_table[fid], sizeof(data_page)); // 每次remove操作之后都会进行persist
            // if(catalog.buckets_virtual_address[bucketID]->bitmap[0] == 0 && catalog.buckets_virtual_address[bucketID]->bitmap[1] == 0){
            //     mergeBucket(bucketID);
            // }
        return 0;
    }
    temp >>= 1;
}
```



`int update(kv kv_pair)`：

该函数起更新的作用，与上面一样，用search函数查找对应的key是否存在，若存在则继续，若不存在则返回-1表示失败。依然与上面一样，调用哈希函数找到桶号BucketID，再将桶的位图赋值给临时变量bit_map[2]，然后在调用vAddr2pmAddr.find()函数传入参数BucketID得到对应的页号fid和偏移量off。然后依然是在桶中遍历15个槽，找到对应的key，然后用kv_pari的value替换原来的value并保存到内存上面。

```C++
catalog.buckets_virtual_address[bucketID]->slot[i].value = kv_pair.value;
page_pointer_table[fid]->page_bucket->inner_kv[index].value = kv_pair.value;
pmem_persist(page_pointer_table[fid], sizeof(data_page)); // 每次update操作之后都会进行persist
return 0;
```

### 3.1.1  类图

![NceYFJ.png](https://s1.ax1x.com/2020/06/27/NceYFJ.png)



## 3.2  Google Test

> gtest是谷歌的C单元测试的框架，能对我们的项目进行简单的单元测试。

​	测试代码TA已经写好，项目完成之后，在test文件夹下进行makefile，然后运行测试ehash_test，	通过所有测试。

​	需要注意的地方：test下的makefile文件只有pm_ehash的编译链接，需要在写完data_page或其他	自己添加的源文件后修改makefile进行编译链接



## 3.3  YCSB测试

> YCSB是雅虎开源的一款通用的性能测试工具。主要用在云端或服务器端的性能评测分析，可以对各类非关系型数据库产品进行相关的性能测试和评估。             
>
> > 在本项目中是一个键值数据库性能测试benchmark，数据文件在workload文件夹下。

 1. 220w-rw-50-50-load.txt表示里面的数据量时220w，读写比位50比50，是load阶段的数据集。

 2. YCSB测试分为load和run阶段。进行测试时需要先读取load的数据集进行数据库初始化，然后再读取run阶段的数据集进行实际增删改查操作。

    - **load**：预先初始化数据库（插入操作）
    - **run**：  运行性能测试（增删改查操作）

	3. INSERT 6284781860667377211是数据集的数据形式，前面表示操作插入，后面表示数据。由于我们的键长度只有8字节，所以加载后面数据时对应复制前8字符长度进对应的键的变量即可

    ### 测试指标：
     	1. load和run阶段的运行时间
     	2. 总操作数
     	3. 增删改查操作的比例
     	4. OPS：每秒操作数



# 四、实验结果

## 4.1 验证方法 

### 4.1.1 挂载操作系统 

* 
* mount -o dax /dev/pmem0 [你的数据地址] -v
* df -h 查看挂载结果

### 4.1.2 make gtest 

​	测试结果如下图：

![NceGo4.jpg](https://s1.ax1x.com/2020/06/27/NceGo4.jpg)

​	由gtest测试可以看出，我们完成了可拓展哈希的基本设计，可以进行YSCB的测试。

### 4.1.3 make ycsb 

​	我们通过读取workload文件夹里的run和load文件，来获取`run`和`load`操作所需的数据，并根据相应	的操作命令进行相应的操作。

​	**1. 读取文件**

​	读取文件后，根据文件格式逐行读取`run`和`load`命令，并将命令分别储存到`runOperation`和 `loadOperation`中。

```c++
loadOperation.clear();
runOperation.clear();
fstream load(loadPath[file_index]);
while (getline(load, line)) {
	loadOperation.push_back(line);
}
load.close();
fstream run(runPath[file_index]);
while (getline(run, line)) {
	runOperation.push_back(line);
}
run.close();
```

​	根据操作命令进行操作

​	根据操作命令，转化成相应的函数进行操作，并统计run/load操作所需的时间

​	

​	**2. load操作**

​	load操作中只有INSERT操作，因此我们只需将INSERT命令转化为可拓展哈希的插入操作即可（ehash->insert()）

```c++
for (int i = 0; i < loadOperation.size(); i++) {
	string order = loadOperation[i];
	string operation = "";
	uint64_t key = 0;
	uint64_t value = 0;
	for (int j = 0; j < order.length();) {
		while (order[j] >= 'A' && order[j] <= 'Z') {
			operation += order[j];
			j++;
		}
		j++;
		for (int k = 0; k < 8; k++) {
			key = key * 10 + order[j] - '0';
			j++;
		}
		for (int k = 0; k < 8; k++) {
			value = value * 10 + order[j] - '0';
			j++;
		}
		j = order.length();
	}
	if (operation == "INSERT") {
		kv temp;
		temp.key = key;
		temp.value = value;
		ehash->insert(temp);
	}
}
```



​	**3. run操作**

​	由于run文件命令格式与load文件命令格式类似，因此，读取时的操作相同。run操作包括INSERT,READ,UPDATE,DELETE增删改查操	作，我们需要将增删改查操作转化为相应的可拓展哈希的操作。

```c++
if (operation == "INSERT") {
	if (ehash->insert(temp) == 0) {
		INSERT_SUCCESS++;
	}
	INSERT++;
} else if (operation == "UPDATE") {
	if (ehash->update(temp) == 0) {
		UPDATE_SUCCESS++;
	}
	UPDATE++;
} else if (operation == "READ") {
	if (ehash->search(key, value) == 0) {
		READ_SUCCESS++;
	}
	READ++;
} else if (operation == "DELETE") {
	if (ehash->remove(key) == 0) {
		DELETE_SUCCESS++;
	}
	DELETE++;
}
```

​	最后通过时钟来记录操作所需的总时间和总操作数等性能参数



​	**load操作性能参数**

```c++
cout << "********************************" << loadPath[file_index] << "*******************************\n";
cout << "load total time : " << (double)(loadEndTime - loadStartTime) / CLOCKS_PER_SEC << "s" << endl;
cout << "load total operations : " << loadOperation.size() << endl;
cout << "load operations per second : " << loadOperation.size() * CLOCKS_PER_SEC / (double)(loadEndTime - 				loadStartTime) << endl;
```

​	

​	**run操作性能参数**

```c++
cout << "run total Time : " << (double)(runEndTime - runStartTime) / CLOCKS_PER_SEC << "s" << endl;
cout << "run total operations : " << runOperation.size() << endl;
cout << "run operations per second : " << runOperation.size() * CLOCKS_PER_SEC / (double)(runEndTime - runStartTime) << endl;
cout << "INSERT : " << INSERT << " INSERT_SUCCESS : " << INSERT_SUCCESS << endl;
cout << "UPDATE : " << UPDATE << " UPDATE_SUCCESS : " << UPDATE_SUCCESS << endl;
cout << "READ : " << READ << " READ_SUCCESS : " << READ_SUCCESS << endl;
cout << "DELETE : " << DELETE << " DELETE_SUCCESS  : " << DELETE_SUCCESS << endl;
cout << "********************************" << "END:" << runPath[file_index] << "*******************************\n";
ehash->selfDestroy();
```



## 4.2 实验结果 

### 4.2.1 make gtest结果 

![NceGo4.jpg](https://s1.ax1x.com/2020/06/27/NceGo4.jpg)



### 4.2.2 make ycsb结果

![Nce8wF.jpg](https://s1.ax1x.com/2020/06/27/Nce8wF.jpg)

![NcelLT.jpg](https://s1.ax1x.com/2020/06/27/NcelLT.jpg)

![NceMQ0.jpg](https://s1.ax1x.com/2020/06/27/NceMQ0.jpg)

# 五、实验总结

本次实验是一个大实验，但其实归根到底，就是在分配页表的时候使用pmem_map_file去形式上代替new，然后在每次修改页表之后，需要进行pmem_persist进行写回。其他的就和设计一个正常的可扩展哈希没有什么区别了。

在设计可扩展哈希的时候，我们需要注意的就是合并桶和分桶的问题。在进行分桶的操作时，对目录项进行一次重新的桶映射是关键，若部分桶没有更新映射，就有可能会在大量数据插入之后导致页表文件的大量分配，导致内存不足。

在合并桶的时候，要注意必须使用局部的桶深度去进行对应桶的查找，并且在相对应的两个桶都是空的时候才可以进行合并。在合并之后，当所有的桶深度都小于全局深度之后才可以进行删除目录。

之后的增删检改都是相对简单的操作了，但是要熟悉C语言的指针操作。

总之，经过本次实验，我们收获了很多。

