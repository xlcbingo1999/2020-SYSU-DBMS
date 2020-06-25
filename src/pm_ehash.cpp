#include "pm_ehash.h"

/**
 * @description: construct a new instance of PmEHash in a default directory
 * @param NULL
 * @return: new instance of PmEHash
 */
/**
 * @description: construct a new instance of PmEHash in a default directory
 * @param NULL
 * @return: new instance of PmEHash
 */
PmEHash::PmEHash()
{

    size_t map_len;
    int is_pmem;
    const char* data_dir = PM_EHASH_DIRECTORY;
    const char* meta_file_dir = META_NAME;
    const char* catalog_file_dir = CATALOG_NAME;

    int size_of_dir = strlen(data_dir) + strlen(meta_file_dir) + 1;
    int size_of_catalog_dir = strlen(data_dir) + strlen(catalog_file_dir) + 2;
    char* final_meta_dir;
    final_meta_dir = new char[size_of_dir];
    int count = 0;
    for (int i = 0; i < strlen(data_dir); ++i) {
        final_meta_dir[count++] = data_dir[i];
    }
    for (int i = 0; i < strlen(meta_file_dir); ++i) {
        final_meta_dir[count++] = meta_file_dir[i];
    }
    final_meta_dir[count] = '\0';

    char* final_catalog_dir;
    final_catalog_dir = new char[size_of_catalog_dir];
    count = 0;
    for (int i = 0; i < strlen(data_dir); ++i) {
        final_catalog_dir[count++] = data_dir[i];
    }
    for (int i = 0; i < strlen(catalog_file_dir); ++i) {
        final_catalog_dir[count++] = catalog_file_dir[i];
    }
    final_catalog_dir[count] = '0';
    final_catalog_dir[count + 1] = '\0';
    // 新建ehash_metadata文件

    std::ifstream fin(final_meta_dir);
    if (!fin) {
        fin.close();
        metadata = (ehash_metadata*)pmem_map_file(final_meta_dir, sizeof(ehash_metadata), PMEM_FILE_CREATE, 0777, &map_len, &is_pmem);
        metadata->max_file_id = 0;
        metadata->catalog_size = 2;
        metadata->global_depth = 1;
        pmem_persist(metadata,sizeof(ehash_metadata));
        allocNewPage(); // 直接获取一个新的页面
        catalog_file_table = new catalog_page_file*[1];
        catalog_file_table[0] = (catalog_page_file*)pmem_map_file(final_catalog_dir, sizeof(catalog_page_file), PMEM_FILE_CREATE, 0777, &map_len, &is_pmem);
        // 直接新建两个桶
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
            catalog_file_table[0]->catalog_item[i].fileId = new_pm_address.fileId;
            catalog_file_table[0]->catalog_item[i].offset = new_pm_address.offset;
        }
        
    } else {
        fin.close();
        recover();
    }
}
/**
 * @description: persist and munmap all data in NVM
 * @param NULL 
 * @return: NULL
 */
PmEHash::~PmEHash()
{
    pmem_persist(metadata, sizeof(ehash_metadata));
    pmem_unmap(metadata, sizeof(ehash_metadata));
    for (int i = 0; i < metadata->max_file_id; ++i) {
        // pmem_persist(page_pointer_table[i], sizeof(data_page));
        pmem_unmap(page_pointer_table[i], sizeof(data_page));
    }
    uint64_t cat_size = 0;
    if(metadata->catalog_size < 512) cat_size = 1;
    else cat_size = metadata->catalog_size / 512;
    for (int i = 0; i < cat_size; ++i){
        pmem_persist(catalog_file_table[i],sizeof(catalog_page_file));
        pmem_unmap(catalog_file_table[i],sizeof(catalog_page_file));
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
    page_pointer_table[fid]->page_bucket[index_bitmap].bitmap[0] = toInsertBucket->bitmap[0];
    page_pointer_table[fid]->page_bucket[index_bitmap].bitmap[1] = toInsertBucket->bitmap[1];
    for (int i = 0; i < BUCKET_SLOT_NUM; ++i) {
        page_pointer_table[fid]->page_bucket[index_bitmap].inner_kv[i].key = toInsertBucket->slot[i].key;
        page_pointer_table[fid]->page_bucket[index_bitmap].inner_kv[i].value = toInsertBucket->slot[i].value;
    }
    pmem_persist(page_pointer_table[fid], sizeof(data_page)); // 每次insert操作之后都会进行persist
    
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
    if (search(key, returnSearchValue) == -1)
        return -1;
    uint64_t bucketID = hashFunc(key);
    uint8_t temp = 128;
    uint32_t fid = vAddr2pmAddr.find(catalog.buckets_virtual_address[bucketID])->second.fileId;

    for (int i = 0; i < 8; ++i) {
        if ((catalog.buckets_virtual_address[bucketID]->bitmap[0] & temp) != 0 && catalog.buckets_virtual_address[bucketID]->slot[i].key == key) {
            catalog.buckets_virtual_address[bucketID]->bitmap[0] &= (~(1 << (7 - i)));
            page_pointer_table[fid]->page_bucket->bitmap[0] &= (~(1 << (7 - i)));
            pmem_persist(page_pointer_table[fid], sizeof(data_page)); // 每次remove操作之后都会进行persist
            // if(catalog.buckets_virtual_address[bucketID]->bitmap[0] == 0 && catalog.buckets_virtual_address[bucketID]->bitmap[1] == 0){
            //     mergeBucket(bucketID);
            // }
            return 0;
        }
        temp >>= 1;
    }
    temp = 128;
    for (int i = 8; i < 15; ++i) {
        if ((catalog.buckets_virtual_address[bucketID]->bitmap[1] & temp) != 0 && catalog.buckets_virtual_address[bucketID]->slot[i].key == key) {
            catalog.buckets_virtual_address[bucketID]->bitmap[1] &= (~(1 << (15 - i)));
            page_pointer_table[fid]->page_bucket->bitmap[1] &= (~(1 << (15 - i)));
            pmem_persist(page_pointer_table[fid], sizeof(data_page)); // 每次remove操作之后都会进行persist
            // if(catalog.buckets_virtual_address[bucketID]->bitmap[0] == 0 && catalog.buckets_virtual_address[bucketID]->bitmap[1] == 0){
            //     mergeBucket(bucketID);
            // }
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
    if (search(kv_pair.key, returnSearchValue) == -1)
        return -1;
    uint64_t bucketID = hashFunc(kv_pair.key);
    uint8_t bit_map[2];
    bit_map[0] = catalog.buckets_virtual_address[bucketID]->bitmap[0];
    bit_map[1] = catalog.buckets_virtual_address[bucketID]->bitmap[1];
    uint32_t fid = vAddr2pmAddr.find(catalog.buckets_virtual_address[bucketID])->second.fileId;
    uint32_t off = vAddr2pmAddr.find(catalog.buckets_virtual_address[bucketID])->second.offset;
    uint32_t index = (off - 2) / 255;
    uint8_t temp = 128;
    for (int i = 0; i < 8; ++i) {
        if ((bit_map[0] & temp) != 0 && catalog.buckets_virtual_address[bucketID]->slot[i].key == kv_pair.key) {
            catalog.buckets_virtual_address[bucketID]->slot[i].value = kv_pair.value;
            page_pointer_table[fid]->page_bucket->inner_kv[index].value = kv_pair.value;
            pmem_persist(page_pointer_table[fid], sizeof(data_page)); // 每次update操作之后都会进行persist
            return 0;
        }
        temp >>= 1;
    }
    temp = 128;
    for (int i = 8; i < 15; ++i) {
        if ((bit_map[1] & temp) != 0 && catalog.buckets_virtual_address[bucketID]->slot[i].key == kv_pair.key) {
            catalog.buckets_virtual_address[bucketID]->slot[i].value = kv_pair.value;
            page_pointer_table[fid]->page_bucket->inner_kv[index].value = kv_pair.value;
            pmem_persist(page_pointer_table[fid], sizeof(data_page)); // 每次update操作之后都会进行persist
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
 * @description: 显示桶的结构，用于debug
 * @param: NULL
 * @return: NULL
 */
void PmEHash::display()
{
    std::set<uint64_t> toprint;
    printf("metadata->global_depth:%ld\n", metadata->global_depth);
    for (int i = 0; i < metadata->catalog_size; ++i) {
        printf("bucket %02d ", i);
        printf("local_depth %ld ", catalog.buckets_virtual_address[i]->local_depth);
        printf("{%02x%02x}", catalog.buckets_virtual_address[i]->bitmap[0], catalog.buckets_virtual_address[i]->bitmap[1]);
        printf("(0x%p)", catalog.buckets_virtual_address[i]);
        uint8_t temp = 128;
        for (int j = 0; j < 8; ++j) {
            if ((catalog.buckets_virtual_address[i]->bitmap[0] & temp) != 0) {
                printf("[%03ld] ", catalog.buckets_virtual_address[i]->slot[j].key);

                toprint.insert(catalog.buckets_virtual_address[i]->slot[j].key);
            }
            temp >>= 1;
        }
        temp = 128;
        for (int j = 8; j < 15; ++j) {
            if ((catalog.buckets_virtual_address[i]->bitmap[1] & temp) != 0) {
                printf("[%03ld] ", catalog.buckets_virtual_address[i]->slot[j].key);
                toprint.insert(catalog.buckets_virtual_address[i]->slot[j].key);
            }
            temp >>= 1;
        }
        printf("\n");
    }
    // std::set<uint64_t>::iterator iter;
    printf("size: %ld\n", toprint.size());
    // for(iter = toprint.begin(); iter != toprint.end(); ++iter){
    //     printf("%ld ", *iter);
    // }
    // printf("\n");
}

/**
 * @description: 用于对输入的键产生哈希值，然后取模求桶号(自己挑选合适的哈希函数处理)
 * @param uint64_t: 输入的键
 * @return: 返回键所属的桶号
 */
uint64_t PmEHash::hashFunc(uint64_t key)
{
    uint64_t bucketID = key & ((1 << metadata->global_depth) - 1); // 位操作，相当于对(1 << metadata->global_depth)求余
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
        // 如果分桶后，插入的桶仍然是满的，就需要再一次分桶
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
            // 正常不会进入这个选项
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
    // 计算分桶时原目录的对应index
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
    // 桶的局部深度自增后，修改目录表项
    uint64_t yu = to_bucket_id % (1 << ori_bucket->local_depth);
    for (int i = 0; i < metadata->catalog_size; ++i) {
        if ((i % (1 << ori_bucket->local_depth)) == yu) {
            catalog.buckets_virtual_address[i] = new_bucket;
            catalog_file_table[i/512]->catalog_item[i%512].fileId = new_pm_address.fileId;
            catalog_file_table[i/512]->catalog_item[i%512].offset = new_pm_address.offset;
        }
    }
    uint64_t to_insert_bucketid;
    kv* temp_kv;
    for (int i = 0; i < BUCKET_SLOT_NUM; ++i) {
        to_insert_bucketid = hashFunc(ori_kv[i].key);
        temp_kv = getFreeKvSlot(catalog.buckets_virtual_address[to_insert_bucketid]);
        temp_kv->key = ori_kv[i].key;
        temp_kv->value = ori_kv[i].value;
    }
    catalog.buckets_pm_address[to_bucket_id].fileId = new_pm_address.fileId;
    catalog.buckets_pm_address[to_bucket_id].offset = new_pm_address.offset;
    catalog_file_table[to_bucket_id/512]->catalog_item[to_bucket_id%512].fileId = new_pm_address.fileId;
    catalog_file_table[to_bucket_id/512]->catalog_item[to_bucket_id%512].offset = new_pm_address.offset;
    
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
    uint64_t mask = (1 << catalog.buckets_virtual_address[bucket_id]->local_depth);
    uint64_t to_bucket_id = (bucket_id & (~mask)) | (bucket_id ^ mask);
    catalog.buckets_virtual_address[to_bucket_id] = catalog.buckets_virtual_address[bucket_id];
    free_list.push(catalog.buckets_virtual_address[bucket_id]);
    catalog.buckets_virtual_address[bucket_id]->local_depth -= 1;
    int i;
    for (i = 0; i < metadata->catalog_size; ++i) {
        if (catalog.buckets_virtual_address[i]->local_depth >= metadata->global_depth)
            break;
    }
    if (i == metadata->catalog_size && i > 2) {
        uint64_t ori_cata_size = metadata->catalog_size;
        ehash_catalog temp_catalog;
        temp_catalog.buckets_pm_address = new pm_address[ori_cata_size / 2];
        temp_catalog.buckets_virtual_address = new pm_bucket*[ori_cata_size / 2];
        for (int j = 0; j < ori_cata_size / 2; ++j) {
            temp_catalog.buckets_pm_address[j] = catalog.buckets_pm_address[j];
            temp_catalog.buckets_virtual_address[j] = catalog.buckets_virtual_address[j];
        }
        catalog.buckets_pm_address = new pm_address[ori_cata_size / 2];
        catalog.buckets_virtual_address = new pm_bucket*[ori_cata_size / 2];
        for (int j = 0; j < ori_cata_size / 2; ++j) {
            catalog.buckets_pm_address[j] = temp_catalog.buckets_pm_address[j];
            catalog.buckets_virtual_address[j] = temp_catalog.buckets_virtual_address[j];
        }
        metadata->catalog_size = ori_cata_size / 2;
        metadata->global_depth -= 1;
    }
}

/**
 * @description: 对目录进行倍增，需要重新生成新的目录文件并复制旧值，然后删除旧的目录文件
 * @param NULL
 * @return: NULL
 */
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
    uint32_t fid = vAddr2pmAddr.find(new_bucket)->second.fileId;
    uint32_t off = vAddr2pmAddr.find(new_bucket)->second.offset;
    uint32_t index_bitmap = (off - 2) / 255;
    page_pointer_table[fid]->bitmap ^= (page_pointer_table[fid]->bitmap & (1 << (15 - index_bitmap)) ^ (1 << (15 - index_bitmap)));
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

/**
 * @description: 读取旧数据文件重新载入哈希，恢复哈希关闭前的状态
 * @param NULL
 * @return: NULL
 */
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

/**
 * @description: 重启时，将所有数据页进行内存映射，设置地址间的映射关系，空闲的和使用的槽位都需要设置 
 * @param NULL
 * @return: NULL
 */
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

/**
 * @description: 删除PmEHash对象所有数据页，目录和元数据文件，主要供gtest使用。即清空所有可扩展哈希的文件数据，不止是内存上的
 * @param NULL
 * @return: NULL
 */
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