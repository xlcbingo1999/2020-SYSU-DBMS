#include "pm_ehash.h"


/**
 * @description: construct a new instance of PmEHash in a default directory
 * @param NULL
 * @return: new instance of PmEHash
 */
PmEHash::PmEHash()
{
    size_t map_len;
    int is_pmem;
    metadata = (ehash_metadata*)pmem_map_file("/mnt/pmemdir/ehash_metadata", sizeof(ehash_metadata), PMEM_FILE_CREATE, 0777, &map_len, &is_pmem);
    metadata->global_depth = 1;
    metadata->max_file_id = 0;
    metadata->catalog_size = 2;
    allocNewPage();
    catalog.buckets_pm_address = new pm_address[2];
    catalog.buckets_virtual_address = new pm_bucket*[2];
    for (int i = 0; i < 2; ++i) {
        pm_address new_pm_address;
        pm_bucket* new_bucket = (pm_bucket*)getFreeSlot(new_pm_address);
        new_bucket->bitmap[0] = 0;
        new_bucket->bitmap[1] = 0;
        new_bucket->local_depth = metadata->global_depth;
        catalog.buckets_pm_address[i].fileId = new_pm_address.fileId;
        catalog.buckets_pm_address[i].offset = new_pm_address.offset;
        catalog.buckets_virtual_address[i] = new_bucket;
    }
}
/**
 * @description: persist and munmap all data in NVM
 * @param NULL 
 * @return: NULL
 */
PmEHash::~PmEHash()
{
    pmem_unmap(metadata, sizeof(ehash_metadata));
    for (int i = 0; i < metadata->max_file_id; ++i) {
        // pmem_persist(page_pointer_table[i], sizeof(data_page));
        pmem_unmap(page_pointer_table[i], sizeof(data_page));
    }
}

/**
 * @description: 插入新的键值对，并将相应位置上的位图置1
 * @param kv: 插入的键值对
 * @return: 0 = insert successfully, -1 = fail to insert(target data with same key exist)
 */
int PmEHash::insert(kv new_kv_pair)
{
    uint64_t returnSearchValue;
    if (search(new_kv_pair.key, returnSearchValue) == 0)
        return -1;
    pm_bucket* toInsertBucket = getFreeBucket(new_kv_pair.key);
    kv* freePlace = getFreeKvSlot(toInsertBucket);
    freePlace->key = new_kv_pair.key;
    freePlace->value = new_kv_pair.value;
    pm_address persist_address = vAddr2pmAddr.find(toInsertBucket)->second;
    uint32_t fid = persist_address.fileId;
    int index_bitmap = (persist_address.offset - 2) / 255;
    // page_pointer_table[fid]->bitmap ^= (page_pointer_table[fid]->bitmap & (1 << (15 - index_bitmap)) ^ (1 << (15 - index_bitmap)));
    page_pointer_table[fid]->page_bucket[index_bitmap].bitmap[0] = toInsertBucket->bitmap[0];
    page_pointer_table[fid]->page_bucket[index_bitmap].bitmap[1] = toInsertBucket->bitmap[1];
    for (int i = 0; i < BUCKET_SLOT_NUM; ++i) {
        page_pointer_table[fid]->page_bucket[index_bitmap].inner_kv[i].key = toInsertBucket->slot[i].key;
        page_pointer_table[fid]->page_bucket[index_bitmap].inner_kv[i].value = toInsertBucket->slot[i].value;
    }
    pmem_persist(page_pointer_table[fid], sizeof(data_page));
    return 0;
}

/**
 * @description: 删除具有目标键的键值对数据，不直接将数据置0，而是将相应位图置0即可
 * @param uint64_t: 要删除的目标键值对的键
 * @return: 0 = removing successfully, -1 = fail to remove(target data doesn't exist)
 */
int PmEHash::remove(uint64_t key)
{
    uint64_t returnSearchValue;
    if (search(key, returnSearchValue) == -1) return -1;
    uint64_t bucketID = hashFunc(key);
    uint8_t temp = 128;
    uint32_t fid = vAddr2pmAddr.find(catalog.buckets_virtual_address[bucketID])->second.fileId;
    
    for(int i = 0; i < 8; ++i){
        if((catalog.buckets_virtual_address[bucketID]->bitmap[0] & temp) != 0 && catalog.buckets_virtual_address[bucketID]->slot[i].key == key){
            catalog.buckets_virtual_address[bucketID]->bitmap[0] &= (~(1 << (7 - i)));
            page_pointer_table[fid]->page_bucket->bitmap[0] &= (~(1 << (7 - i)));
            if(catalog.buckets_virtual_address[bucketID]->bitmap[0] == 0 && catalog.buckets_virtual_address[bucketID]->bitmap[1] == 0){
                mergeBucket(bucketID);
            }
            return 0;
        }
        temp >>= 1;
    }
    temp = 128;
    for(int i = 8; i < 15; ++i){
        if((catalog.buckets_virtual_address[bucketID]->bitmap[1] & temp) != 0 && catalog.buckets_virtual_address[bucketID]->slot[i].key == key){
            catalog.buckets_virtual_address[bucketID]->bitmap[1] &= (~(1 << (15 - i)));
            page_pointer_table[fid]->page_bucket->bitmap[1] &= (~(1 << (15 - i)));
            if(catalog.buckets_virtual_address[bucketID]->bitmap[0] == 0 && catalog.buckets_virtual_address[bucketID]->bitmap[1] == 0){
                mergeBucket(bucketID);
            }
            return 0;
        }
        temp >>= 1;
    }
    return -1;
}

/**
 * @description: 更新现存的键值对的值
 * @param kv: 更新的键值对，有原键和新值
 * @return: 0 = update successfully, -1 = fail to update(target data doesn't exist)
 */
int PmEHash::update(kv kv_pair)
{
    uint64_t returnSearchValue;
    if (search(kv_pair.key, returnSearchValue) == -1) return -1;
    uint64_t bucketID = hashFunc(kv_pair.key);
    uint8_t bit_map[2];
    bit_map[0] = catalog.buckets_virtual_address[bucketID]->bitmap[0];
    bit_map[1] = catalog.buckets_virtual_address[bucketID]->bitmap[1];
    uint32_t fid = vAddr2pmAddr.find(catalog.buckets_virtual_address[bucketID])->second.fileId;
    uint32_t off = vAddr2pmAddr.find(catalog.buckets_virtual_address[bucketID])->second.offset;
    uint32_t index = (off - 2) / 255;
    uint8_t temp = 128;
    for(int i = 0; i < 8; ++i){
        if((bit_map[0] & temp) != 0 && catalog.buckets_virtual_address[bucketID]->slot[i].key == kv_pair.key){
            catalog.buckets_virtual_address[bucketID]->slot[i].value = kv_pair.value;
            page_pointer_table[fid]->page_bucket->inner_kv[index].value = kv_pair.value;
            return 0;
        }
        temp >>= 1;
    }
    temp = 128;
    for(int i = 8; i < 15; ++i){
        if((bit_map[1] & temp) != 0 && catalog.buckets_virtual_address[bucketID]->slot[i].key == kv_pair.key){
            catalog.buckets_virtual_address[bucketID]->slot[i].value = kv_pair.value;
            page_pointer_table[fid]->page_bucket->inner_kv[index].value = kv_pair.value;
            return 0;
        }
        temp >>= 1;
    }
    return -1;
}

/**
 * @description: 查找目标键值对数据，将返回值放在参数里的引用类型进行返回
 * @param uint64_t: 查询的目标键
 * @param uint64_t&: 查询成功后返回的目标值
 * @return: 0 = search successfully, -1 = fail to search(target data doesn't exist) 
 */
int PmEHash::search(uint64_t key, uint64_t& return_val)
{
    uint64_t to_find_bucket_id = hashFunc(key);
    uint8_t bucket_map[2];
    bucket_map[0] = catalog.buckets_virtual_address[to_find_bucket_id]->bitmap[0];
    bucket_map[1] = catalog.buckets_virtual_address[to_find_bucket_id]->bitmap[1];
    uint8_t toInsertSlotIndex;
    uint8_t temp = 128;
    for (toInsertSlotIndex = 0; toInsertSlotIndex < 8; ++toInsertSlotIndex) {
        if ((bucket_map[0] & temp) != 0) {
            if (catalog.buckets_virtual_address[to_find_bucket_id]->slot[toInsertSlotIndex].key == key) {
                return_val = catalog.buckets_virtual_address[to_find_bucket_id]->slot[toInsertSlotIndex].value;
                return 0;
            }
        }
        temp >>= 1;
    }
    temp = 128;
    for (toInsertSlotIndex = 8; toInsertSlotIndex < 15; ++toInsertSlotIndex) {
        if ((bucket_map[1] & temp) != 0) {
            if (catalog.buckets_virtual_address[to_find_bucket_id]->slot[toInsertSlotIndex].key == key) {
                return_val = catalog.buckets_virtual_address[to_find_bucket_id]->slot[toInsertSlotIndex].value;
                return 0;
            }
        }
        temp >>= 1;
    }
    return -1;
}

/**
 * @description: 用于对输入的键产生哈希值，然后取模求桶号(自己挑选合适的哈希函数处理)
 * @param uint64_t: 输入的键
 * @return: 返回键所属的桶号
 */
uint64_t PmEHash::hashFunc(uint64_t key)
{
    uint64_t bucketID = key & ((1 << metadata->global_depth) - 1);
    return bucketID;
}

/**
 * @description: 获得供插入的空闲的桶，无空闲桶则先分裂桶然后再返回空闲的桶
 * @param uint64_t: 带插入的键
 * @return: 空闲桶的虚拟地址
 */
pm_bucket* PmEHash::getFreeBucket(uint64_t key)
{
    uint64_t bucketID = hashFunc(key);
    while (catalog.buckets_virtual_address[bucketID]->bitmap[0] == 255 && catalog.buckets_virtual_address[bucketID]->bitmap[1] == 254) {
        splitBucket(bucketID);
        bucketID = hashFunc(key);
    }
    return catalog.buckets_virtual_address[bucketID];
}

/**
 * @description: 获得空闲桶内第一个空闲的位置供键值对插入
 * @param pm_bucket* bucket
 * @return: 空闲键值对位置的虚拟地址
 */
kv* PmEHash::getFreeKvSlot(pm_bucket* bucket)
{
    uint8_t bucket_map[2];
    bucket_map[0] = bucket->bitmap[0];
    bucket_map[1] = bucket->bitmap[1];
    uint8_t toInsertSlotIndex;
    uint8_t temp = 128;
    for (toInsertSlotIndex = 0; toInsertSlotIndex < 8; ++toInsertSlotIndex) {
        //printf("%d\n",(bucket_map[0] & temp) == 0);
        if ((bucket_map[0] & temp) == 0) {
            bucket->bitmap[0] ^= (bucket->bitmap[0] & (1 << (7 - toInsertSlotIndex)) ^ (1 << (7 - toInsertSlotIndex)));
            kv* to_return_kv = &bucket->slot[toInsertSlotIndex];
            return to_return_kv;
        } else {
            temp >>= 1;
        }
    }
    if (temp == 0) {
        temp = 128;
        for (toInsertSlotIndex = 8; toInsertSlotIndex < 15; ++toInsertSlotIndex) {
            if ((bucket_map[1] & temp) == 0) {
                bucket->bitmap[1] ^= (bucket->bitmap[1] & (1 << (15 - toInsertSlotIndex)) ^ (1 << (15 - toInsertSlotIndex)));
                kv* to_return_kv = &bucket->slot[toInsertSlotIndex];
                return to_return_kv;
            } else {
                temp >>= 1;
            }
        }
        if (temp == 1) {
            printf("Wrong!");
            return NULL;
        }
    }
    return NULL;
}

/**
 * @description: 桶满后进行分裂操作，可能触发目录的倍增
 * @param uint64_t: 目标桶在目录中的序号
 * @return: NULL
 */
void PmEHash::splitBucket(uint64_t bucket_id)
{
    pm_bucket* ori_bucket = catalog.buckets_virtual_address[bucket_id];
    kv* ori_kv = new kv[BUCKET_SLOT_NUM];
    for (int i = 0; i < BUCKET_SLOT_NUM; ++i) {
        ori_kv[i].key = ori_bucket->slot[i].key;
        ori_kv[i].value = ori_bucket->slot[i].value;
    }
    ori_bucket->bitmap[0] = 0;
    ori_bucket->bitmap[1] = 0;
    uint64_t mask = (1 << ori_bucket->local_depth);
    uint64_t to_bucket_id = (bucket_id & (~mask)) | (bucket_id ^ mask);
    if (catalog.buckets_virtual_address[to_bucket_id] != ori_bucket) {
        extendCatalog();
    }
    pm_address new_pm_address;
    pm_bucket* new_bucket = (pm_bucket*)getFreeSlot(new_pm_address);
    ori_bucket->local_depth += 1;
    new_bucket->local_depth = ori_bucket->local_depth;
    new_bucket->bitmap[0] = 0;
    new_bucket->bitmap[1] = 0;
    catalog.buckets_virtual_address[to_bucket_id] = new_bucket;
    uint64_t to_insert_bucketid;
    kv* temp_kv;
    for (int i = 0; i < BUCKET_SLOT_NUM; ++i) {
        to_insert_bucketid = hashFunc(ori_kv[i].key);
        // to_insert_bucketid = hashFunc(ori_kv[i].key) & ((1 << ori_bucket->local_depth) - 1);
        temp_kv = getFreeKvSlot(catalog.buckets_virtual_address[to_insert_bucketid]);
        temp_kv->key = ori_kv[i].key;
        temp_kv->value = ori_kv[i].value;
    }
    catalog.buckets_pm_address[to_bucket_id].fileId = new_pm_address.fileId;
    catalog.buckets_pm_address[to_bucket_id].offset = new_pm_address.offset;

    uint32_t new_fid = new_pm_address.fileId;
    int index_bitmap = (new_pm_address.offset - 2) / 255;
    page_pointer_table[new_fid]->bitmap ^= (page_pointer_table[new_fid]->bitmap & (1 << (15 - index_bitmap)) ^ (1 << (15 - index_bitmap)));
    page_pointer_table[new_fid]->page_bucket[index_bitmap].bitmap[0] = new_bucket->bitmap[0];
    page_pointer_table[new_fid]->page_bucket[index_bitmap].bitmap[1] = new_bucket->bitmap[1];
    for (int i = 0; i < BUCKET_SLOT_NUM; ++i) {
        page_pointer_table[new_fid]->page_bucket[index_bitmap].inner_kv[i].key = new_bucket->slot[i].key;
        page_pointer_table[new_fid]->page_bucket[index_bitmap].inner_kv[i].value = new_bucket->slot[i].value;
    }
    pmem_persist(page_pointer_table[new_fid], sizeof(data_page));

    pm_address ori_pm_address = vAddr2pmAddr.find(ori_bucket)->second;
    uint32_t ori_fid = ori_pm_address.fileId;
    index_bitmap = (ori_pm_address.offset - 2) / 255;
    page_pointer_table[ori_fid]->bitmap ^= (page_pointer_table[ori_fid]->bitmap & (1 << (15 - index_bitmap)) ^ (1 << (15 - index_bitmap)));
    page_pointer_table[ori_fid]->page_bucket[index_bitmap].bitmap[0] = ori_bucket->bitmap[0];
    page_pointer_table[ori_fid]->page_bucket[index_bitmap].bitmap[1] = ori_bucket->bitmap[1];
    for (int i = 0; i < BUCKET_SLOT_NUM; ++i) {
        page_pointer_table[ori_fid]->page_bucket[index_bitmap].inner_kv[i].key = ori_bucket->slot[i].key;
        page_pointer_table[ori_fid]->page_bucket[index_bitmap].inner_kv[i].value = ori_bucket->slot[i].value;
    }
    pmem_persist(page_pointer_table[ori_fid], sizeof(data_page));
}

/**
 * @description: 桶空后，回收桶的空间，并设置相应目录项指针
 * @param uint64_t: 桶号
 * @return: NULL
 */
void PmEHash::mergeBucket(uint64_t bucket_id)
{
}

/**
 * @description: 对目录进行倍增，需要重新生成新的目录文件并复制旧值，然后删除旧的目录文件
 * @param NULL
 * @return: NULL
 */
void PmEHash::extendCatalog()
{
    uint64_t ori_cata_size = metadata->catalog_size;
    uint64_t free_bucket_size = free_list.size();
    ehash_catalog temp_catalog;
    temp_catalog.buckets_pm_address = new pm_address[ori_cata_size];
    temp_catalog.buckets_virtual_address = new pm_bucket*[ori_cata_size];
    for (int i = 0; i < ori_cata_size; ++i) {
        temp_catalog.buckets_pm_address[i] = catalog.buckets_pm_address[i];
        temp_catalog.buckets_virtual_address[i] = catalog.buckets_virtual_address[i];
    }
    catalog.buckets_pm_address = new pm_address[ori_cata_size * 2];
    catalog.buckets_virtual_address = new pm_bucket*[ori_cata_size * 2];
    for (int i = 0; i < ori_cata_size; ++i) {
        catalog.buckets_pm_address[i] = temp_catalog.buckets_pm_address[i];
        catalog.buckets_virtual_address[i] = temp_catalog.buckets_virtual_address[i];
        catalog.buckets_pm_address[ori_cata_size + i] = temp_catalog.buckets_pm_address[i];
        catalog.buckets_virtual_address[ori_cata_size + i] = temp_catalog.buckets_virtual_address[i];
    }
    metadata->catalog_size = ori_cata_size * 2;
    metadata->global_depth += 1;
}

/**
 * @description: 获得一个可用的数据页的新槽位供哈希桶使用，如果没有则先申请新的数据页
 * @param pm_address&: 新槽位的持久化文件地址，作为引用参数返回
 * @return: 新槽位的虚拟地址
 */
void* PmEHash::getFreeSlot(pm_address& new_address)
{
    if (free_list.empty()) {
        allocNewPage();
    }
    pm_bucket* new_bucket = free_list.front();
    new_bucket->local_depth = metadata->global_depth; // maybe problem.
    free_list.pop();
    new_address = vAddr2pmAddr.find(new_bucket)->second;
    return new_bucket;
}

/**
 * @description: 申请新的数据页文件，并把所有新产生的空闲槽的地址放入free_list等数据结构中
 * @param NULL
 * @return: NULL
 */
void PmEHash::allocNewPage()
{
    std::string file_name = "/mnt/pmemdir/file_name";
    uint64_t name_id = metadata->max_file_id;
    std::string name_id_str = std::to_string(name_id);
    file_name += name_id_str;
    const char* file_name_c = file_name.c_str();
    size_t map_len;
    int is_pmem;
    data_page* new_page = (data_page*)pmem_map_file(file_name_c,sizeof(data_page),PMEM_FILE_CREATE, 0777, &map_len, &is_pmem);
    new_page->bitmap = 0;
    for (int i = 0; i < 14; ++i) {
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
    metadata->max_file_id = metadata->max_file_id + 1;
}

/**
 * @description: 读取旧数据文件重新载入哈希，恢复哈希关闭前的状态
 * @param NULL
 * @return: NULL
 */
void PmEHash::recover()
{
}

/**
 * @description: 重启时，将所有数据页进行内存映射，设置地址间的映射关系，空闲的和使用的槽位都需要设置 
 * @param NULL
 * @return: NULL
 */
void PmEHash::mapAllPage()
{
}

/**
 * @description: 删除PmEHash对象所有数据页，目录和元数据文件，主要供gtest使用。即清空所有可扩展哈希的文件数据，不止是内存上的
 * @param NULL
 * @return: NULL
 */
void PmEHash::selfDestory()
{
    std::string file_name = "/mnt/pmemdir/file_name";
    uint64_t name_id = metadata->max_file_id;
    for(uint64_t i = 0; i < name_id; ++i){
        std::string name_id_str = std::to_string(i);
        file_name += name_id_str;
        const char* file_name_c = file_name.c_str();
        std::remove(file_name_c);
    }
    std::remove("/mnt/pmemdir/ehash_metadata");
    for(uint64_t i = 0; i < name_id; ++i){
        page_pointer_table[i] = NULL;
    }
    for(int i = 0; i < metadata->catalog_size; ++i){
        catalog.buckets_pm_address[i].fileId = 0;
        catalog.buckets_pm_address[i].offset = 0;
        catalog.buckets_virtual_address[i]->bitmap[0] = 0;
        catalog.buckets_virtual_address[i]->bitmap[1] = 0;
        catalog.buckets_virtual_address[i]->local_depth = 0;
        for(int j = 0; j < BUCKET_SLOT_NUM; ++j){
            catalog.buckets_virtual_address[i]->slot[j].key = 0;
            catalog.buckets_virtual_address[i]->slot[j].value = 0;
        }
        catalog.buckets_virtual_address[i] = NULL;
    }
    vAddr2pmAddr.clear();
    pmAddr2vAddr.clear();
    int siz = free_list.size();
    for(int i = 0; i < siz; ++i){
        free_list.pop();
    }
    metadata->global_depth = 0;
    metadata->max_file_id = 0;
    metadata->catalog_size = 0;
}

// void my_swap(uint64_t& a,uint64_t& b){
//     uint64_t temp = a;
//     a = b;
//     b = temp;
// }