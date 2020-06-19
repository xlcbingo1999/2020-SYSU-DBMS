#include "pm_ehash.h"

#include <libpmem.h>

#include <iterator>
/**
 * @description: construct a new instance of PmEHash in a default directory
 * @param NULL
 * @return: new instance of PmEHash
 */
PmEHash::PmEHash() {
    struct data_page* new_page = new data_page;
    metadata->max_file_id = 1;
    metadata->catalog_size = 0;
    metadata->global_depth = 1;
    struct pm_bucket* bucket0 = new pm_bucket;
    bucket0->local_depth = 1;
    bucket0->bitmap[0] = 0;
    bucket0->bitmap[1] = 0;
    free_list.push(bucket0);
    struct pm_bucket* bucket1 = new pm_bucket;
    bucket1->local_depth = 1;
    bucket1->bitmap[0] = 0;
    bucket1->bitmap[1] = 0;
    free_list.push(bucket1);
    catalog.buckets_pm_address = new pm_address[2];
    catalog.buckets_pm_address[0].fileId = 0;
    catalog.buckets_pm_address[0].offset = 0;
    catalog.buckets_pm_address[1].fileId = 0;
    catalog.buckets_pm_address[1].offset = 256;
    catalog.buckets_virtual_address = new pm_bucket*[2];
    catalog.buckets_virtual_address[0] = bucket0;
    catalog.buckets_virtual_address[1] = bucket1;
    vAddr2pmAddr.insert(std::make_pair(catalog.buckets_virtual_address[0], catalog.buckets_pm_address[0]));
    vAddr2pmAddr.insert(std::make_pair(catalog.buckets_virtual_address[1], catalog.buckets_pm_address[1]));
    pmAddr2vAddr.insert(std::make_pair(catalog.buckets_pm_address[0], catalog.buckets_virtual_address[0]));
    pmAddr2vAddr.insert(std::make_pair(catalog.buckets_pm_address[1], catalog.buckets_virtual_address[1]));
}
/**
 * @description: persist and munmap all data in NVM
 * @param NULL 
 * @return: NULL
 */
PmEHash::~PmEHash() {
}

/**
 * @description: 插入新的键值对，并将相应位置上的位图置1
 * @param kv: 插入的键值对
 * @return: 0 = insert successfully, -1 = fail to insert(target data with same key exist)
 */
int PmEHash::insert(kv new_kv_pair) {
    uint64_t returnSearchValue;
    if (search(new_kv_pair.key, returnSearchValue) == 0) return -1;
    pm_bucket* toInsertBucket = getFreeBucket(new_kv_pair.key);
    kv* freePlace = getFreeKvSlot(toInsertBucket);
    freePlace->key = new_kv_pair.key;
    freePlace->value = new_kv_pair.value;
    pmem_persist(new_kv_data, map_len);
    return 0;
}

/**
 * @description: 删除具有目标键的键值对数据，不直接将数据置0，而是将相应位图置0即可
 * @param uint64_t: 要删除的目标键值对的键
 * @return: 0 = removing successfully, -1 = fail to remove(target data doesn't exist)
 */
int PmEHash::remove(uint64_t key) {
    return 1;
}
/**
 * @description: 更新现存的键值对的值
 * @param kv: 更新的键值对，有原键和新值
 * @return: 0 = update successfully, -1 = fail to update(target data doesn't exist)
 */
int PmEHash::update(kv kv_pair) {
    return 1;
}
/**
 * @description: 查找目标键值对数据，将返回值放在参数里的引用类型进行返回
 * @param uint64_t: 查询的目标键
 * @param uint64_t&: 查询成功后返回的目标值
 * @return: 0 = search successfully, -1 = fail to search(target data doesn't exist) 
 */
int PmEHash::search(uint64_t key, uint64_t& return_val) {
    return 1;
}

/**
 * @description: 用于对输入的键产生哈希值，然后取模求桶号(自己挑选合适的哈希函数处理)
 * @param uint64_t: 输入的键
 * @return: 返回键所属的桶号
 */
uint64_t PmEHash::hashFunc(uint64_t key) {
    uint64_t bucketID = key & ((1 << metadata->global_depth) - 1);
    return bucketID;
}

/**
 * @description: 获得供插入的空闲的桶，无空闲桶则先分裂桶然后再返回空闲的桶
 * @param uint64_t: 带插入的键
 * @return: 空闲桶的虚拟地址
 */
pm_bucket* PmEHash::getFreeBucket(uint64_t key) {
    uint64_t bucketID = hashFunc(key);
    // 先使用位图看看需不需要split
}

/**
 * @description: 获得空闲桶内第一个空闲的位置供键值对插入
 * @param pm_bucket* bucket
 * @return: 空闲键值对位置的虚拟地址
 */
kv* PmEHash::getFreeKvSlot(pm_bucket* bucket) {
    uint8_t bucket_map[2];
    bucket_map[0] = bucket->bitmap[0];
    bucket_map[1] = bucket->bitmap[1];
    uint8_t toInsertSlotIndex;
    uint8_t temp = 256;
    for (toInsertSlotIndex = 0; toInsertSlotIndex < 8; ++toInsertSlotIndex) {
        if (bucket_map[0] & temp == 0) {
            break;
        } else {
            temp >> 1;
        }
    }
    if (temp == 0) {
        temp = 256;
        for (toInsertSlotIndex = 8; toInsertSlotIndex < 15; ++toInsertSlotIndex) {
            if (bucket_map[1] & temp == 0) {
                break;
            } else {
                temp >> 1;
            }
        }
        if (temp == 1) {
            // 没有槽可以用了，分裂bucket
            printf("Wrong!");
            return NULL;
        }
    }
    size_t map_len;
    int is_pmem;
    kv* new_kv_data = pmem_map_file(PATH, sizeof(kv), PMEM_FILE_CREATE, 0777, &map_len, &is_pmem);
    return new_kv_data;
}

/**
 * @description: 桶满后进行分裂操作，可能触发目录的倍增
 * @param uint64_t: 目标桶在目录中的序号
 * @return: NULL
 */
void PmEHash::splitBucket(uint64_t bucket_id) {
}

/**
 * @description: 桶空后，回收桶的空间，并设置相应目录项指针
 * @param uint64_t: 桶号
 * @return: NULL
 */
void PmEHash::mergeBucket(uint64_t bucket_id) {
}

/**
 * @description: 对目录进行倍增，需要重新生成新的目录文件并复制旧值，然后删除旧的目录文件
 * @param NULL
 * @return: NULL
 */
void PmEHash::extendCatalog() {
    uint64_t ori_cata_size = metadata->catalog_size;
    uint64_t free_bucket_size = free_list.size();
    ehash_catalog temp_catalog;
    temp_catalog.buckets_pm_address = new pm_address[ori_cata_size];
    temp_catalog.buckets_virtual_address = new pm_bucket*[ori_cata_size];
    for(int i = 0; i < ori_cata_size; ++i){
        temp_catalog.buckets_pm_address[i] = catalog.buckets_pm_address[i];
        temp_catalog.buckets_virtual_address[i] = catalog.buckets_virtual_address[i];
    }
    catalog.buckets_pm_address = new pm_address[ori_cata_size * 2];
    catalog.buckets_virtual_address = new pm_bucket*[ori_cata_size * 2];
    for(int i = 0; i < ori_cata_size; ++i){
        catalog.buckets_pm_address[i] = temp_catalog.buckets_pm_address[i];
        catalog.buckets_virtual_address[i] = temp_catalog.buckets_virtual_address[i];
    }
    if(ori_cata_size <= free_bucket_size){
        for(int i = 0; i < ori_cata_size; ++i){
            
        }
    } else {
        
    }
    
}

/**
 * @description: 获得一个可用的数据页的新槽位供哈希桶使用，如果没有则先申请新的数据页
 * @param pm_address&: 新槽位的持久化文件地址，作为引用参数返回
 * @return: 新槽位的虚拟地址
 */
void* PmEHash::getFreeSlot(pm_address& new_address) {
}

/**
 * @description: 申请新的数据页文件，并把所有新产生的空闲槽的地址放入free_list等数据结构中
 * @param NULL
 * @return: NULL
 */
void PmEHash::allocNewPage() {
    std::string file_name = "/mnt/pmemdir/file_name";
    uint64_t name_id = metadata->max_file_id;
    std::string name_id_str = std::to_string(name_id);
    file_name += name_id_str;
    const char *file_name_c = file_name.c_str();
    data_page *new_page = new data_page(file_name_c);
    new_page->bitmap = 0;
    for(int i = 0; i < 14; ++i){
        new_page->unused_byte_in_data_page[i] = 0;
    }
    data_page *temp_page_table[name_id];
    for(int i = 0 ; i < name_id; ++i){
        temp_page_table[i] = page_pointer_table[i];
    }
    page_pointer_table = new data_page* [name_id+1];
    for(int i = 0 ; i < name_id; ++i){
        page_pointer_table[i] = temp_page_table[i];
    }
    page_pointer_table[name_id] = new_page;
    struct pm_bucket *new_bucket[16];
    for(int j = 0; j < 16; ++j){
        new_bucket[j]->bitmap[0] = 0;
        new_bucket[j]->bitmap[1] = 0;
        for(int i = 0; i < BUCKET_SLOT_NUM; ++i){
            new_bucket[j]->slot[i].key = 0;
            new_bucket[j]->slot[i].value = 0;
        }
        free_list.push(new_bucket[j]);
    }
    struct pm_address new_address[16];
    for(int j = 0; j < 16; ++j){
        new_address[j].fileId = metadata->max_file_id;
        new_address[j].offset = 2 + j * 255;
        vAddr2pmAddr.insert(std::make_pair(new_bucket[j],new_address[j]));
        pmAddr2vAddr.insert(std::make_pair(new_address[j],new_bucket[j]));
    }
    metadata->max_file_id = metadata->max_file_id + 1;
}

/**
 * @description: 读取旧数据文件重新载入哈希，恢复哈希关闭前的状态
 * @param NULL
 * @return: NULL
 */
void PmEHash::recover() {
}

/**
 * @description: 重启时，将所有数据页进行内存映射，设置地址间的映射关系，空闲的和使用的槽位都需要设置 
 * @param NULL
 * @return: NULL
 */
void PmEHash::mapAllPage() {
}

/**
 * @description: 删除PmEHash对象所有数据页，目录和元数据文件，主要供gtest使用。即清空所有可扩展哈希的文件数据，不止是内存上的
 * @param NULL
 * @return: NULL
 */
void PmEHash::selfDestory() {
}