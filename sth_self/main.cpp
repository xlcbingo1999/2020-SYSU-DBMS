
#include <cstdint>
#include <fstream>
#include <iostream>
#include <libpmem.h>
#include <map>
#include <queue>
#include <set>
#include <stdio.h>
#include <string>
#include <time.h>
#include <unistd.h>
#include <vector>

#define BUCKET_SLOT_NUM 15
#define DEFAULT_CATALOG_SIZE 16
#define META_NAME "pm_ehash_metadata";
#define CATALOG_NAME "pm_ehash_catalog";
#define PM_EHASH_DIRECTORY ""; // add your own directory path to store the pm_ehash
// #define CLOCKS_PER_SEC ((__clock_t)1000)
using std::cout;
using std::endl;
using std::fstream;
using std::map;
using std::queue;
using std::string;
using std::vector;
#define DATA_PAGE_SLOT_NUM 16

// use pm_address to locate the data in the page

typedef struct bucket_inner_kv {
    uint64_t key;
    uint64_t value;
} bucket_inner_kv;

typedef struct page_inner_bucket {
    // 32bytes
    uint8_t bitmap[2];
    bucket_inner_kv inner_kv[15];
    uint8_t unused_byte_in_inner_bucket[13];
} page_inner_bucket;

// uncompressed page format design to store the buckets of PmEHash
// one slot stores one bucket of PmEHash
typedef struct data_page {
    // fixed-size record design
    // uncompressed page format
    uint16_t bitmap;
    page_inner_bucket page_bucket[DATA_PAGE_SLOT_NUM];
    uint8_t unused_byte_in_data_page[14];
} data_page;

/* 
---the physical address of data in NVM---
fileId: 1-N, the data page name
offset: data offset in the file
*/
typedef struct pm_address {
    uint32_t fileId;
    uint32_t offset;
    bool operator<(const pm_address& o) const
    {
        return (fileId < o.fileId) || (fileId == o.fileId && offset < o.offset);
    }
} pm_address;

/*
the data entry stored by the  ehash
*/
typedef struct kv {
    uint64_t key;
    uint64_t value;
} kv;

typedef struct pm_bucket {
    uint64_t local_depth;
    uint8_t bitmap[BUCKET_SLOT_NUM / 8 + 1]; // one bit for each slot
    kv slot[BUCKET_SLOT_NUM]; // one slot for one kv-pair
    // bool operator < (const pm_address &o) const {
    //     return (fileId < o.fileId) || (fileId == o.fileId && offset < o.offset);
    // }
} pm_bucket;

// in ehash_catalog, the virtual address of buckets_pm_address[n] is stored in buckets_virtual_address
// buckets_pm_address: open catalog file and store the virtual address of file
// buckets_virtual_address: store virtual address of bucket that each buckets_pm_address points to
typedef struct ehash_catalog {
    pm_address* buckets_pm_address; // pm address array of buckets
    pm_bucket** buckets_virtual_address; // virtual address of buckets that buckets_pm_address point to
} ehash_catalog;

typedef struct ehash_metadata {
    uint64_t max_file_id; // next file id that can be allocated
    uint64_t catalog_size; // the catalog size of catalog file(amount of data entry)
    uint64_t global_depth; // global depth of PmEHash
} ehash_metadata;

class PmEHash {
private:
    data_page** page_pointer_table;

    ehash_metadata* metadata; // virtual address of metadata, mapping the metadata file
    ehash_catalog catalog; // the catalog of hash

    queue<pm_bucket*> free_list; //all free slots in data pages to store buckets
    map<pm_bucket*, pm_address> vAddr2pmAddr; // map virtual address to pm_address, used to find specific pm_address
    map<pm_address, pm_bucket*> pmAddr2vAddr; // map pm_address to virtual address, used to find specific virtual address

    uint64_t hashFunc(uint64_t key);

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
    void display();
    void selfDestory();
};

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
    // pmem_persist(metadata, sizeof(ehash_metadata));
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
    if (search(key, returnSearchValue) == -1)
        return -1;
    uint64_t bucketID = hashFunc(key);
    uint8_t temp = 128;
    uint32_t fid = vAddr2pmAddr.find(catalog.buckets_virtual_address[bucketID])->second.fileId;

    for (int i = 0; i < 8; ++i) {
        if ((catalog.buckets_virtual_address[bucketID]->bitmap[0] & temp) != 0 && catalog.buckets_virtual_address[bucketID]->slot[i].key == key) {
            catalog.buckets_virtual_address[bucketID]->bitmap[0] &= (~(1 << (7 - i)));
            page_pointer_table[fid]->page_bucket->bitmap[0] &= (~(1 << (7 - i)));
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
            return 0;
        }
        temp >>= 1;
    }
    temp = 128;
    for (int i = 8; i < 15; ++i) {
        if ((bit_map[1] & temp) != 0 && catalog.buckets_virtual_address[bucketID]->slot[i].key == kv_pair.key) {
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
 * @description: 用于display
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
    uint64_t yu = to_bucket_id % (1 << ori_bucket->local_depth);
    for (int i = 0; i < metadata->catalog_size; ++i) {
        if ((i % (1 << ori_bucket->local_depth)) == yu) {
            catalog.buckets_virtual_address[i] = new_bucket;
        }
    }
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
    data_page* new_page = (data_page*)pmem_map_file(file_name_c, sizeof(data_page), PMEM_FILE_CREATE, 0777, &map_len, &is_pmem);
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
    for (uint64_t i = 0; i < name_id; ++i) {
        file_name = "/mnt/pmemdir/file_name";
        std::string name_id_str = std::to_string(i);
        file_name += name_id_str;
        const char* file_name_c = file_name.c_str();
        std::remove(file_name_c);
    }
    std::remove("/mnt/pmemdir/ehash_metadata");
    for (uint64_t i = 0; i < name_id; ++i) {
        page_pointer_table[i] = NULL;
    }
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
    vAddr2pmAddr.clear();
    pmAddr2vAddr.clear();
    int siz = free_list.size();
    for (int i = 0; i < siz; ++i) {
        free_list.pop();
    }
    metadata->global_depth = 0;
    metadata->max_file_id = 0;
    metadata->catalog_size = 0;
}

void my_swap(uint64_t& a, uint64_t& b)
{
    uint64_t temp = a;
    a = b;
    b = temp;
}

// int main()
// {
//     srand((unsigned)time(0));
//     PmEHash* pmh = new PmEHash;
//     int num = 1024;
//     kv aaaa;
//     uint64_t qq[1024];
//     // uint64_t qq[10000] = {17444,13944,549,30645,45389,39224,42456,189,52461,2829,7764,18516,11045,4116,245,54776,38829,36,20756,52920,34989,7589,344,42045,43701,50196,84,11684,19620,39621,19901,10020,25301,920,3045,53844,2136,59069,5204,3269,29,16661,29949,32781,23736,44964,24356,981,4920,2324,22221,5061,24045,25941,120,749,10629,34245,2724,37269,24984,8484,6581,36501,4245,60045,24669,63524,13476,49304,12564,10836,63021,65556,2045,1869,53381,1316,33144,48861,17181,38045,2229,276,3864,1109,26916,59556,27245,15396,12789,4376,13245,26264,101,41229,17976,36884,56189,55716,6420,1701,3384,62520,40824,14420,61524,28581,1245,32061,381,23429,22520,7245,48420,47109,17709,9821,27909,216,65045,6744,45816,18245,51096,11256,3741,30996,13016,35364,26589,21629,5496,52004,30296,55245,1044,16404,14661,11901,58584,8669,31349,861,3989,46245,29604,12341,40421,7416,2936,58101,6261,64536,16920,33509,45,21045,2621,10221,20184,1784,15896,42869,3501,6909,8301,64029,44541,24,2421,5349,18789,56664,38436,36120,4644,57620,21924,49749,54309,35741,9429,57141,41636,1541,27576,44120,47544,4781,11469,28920,40020,1956,47981,20469,23124,62021,7076,21,9236,696,14904,7941,16149,50645,1620,9045,37656,19064,13709,3156,60536,2520,14181,34616,21336,19341,3620,1464,31704,28244,141,5949,8856,12120,4509,15149,22821,164,61029,8120,25620,596,32420,5645,420,33876,10424,1176,9624,51549,46676,645,69,29261,309,461,804,6104,5796,43284,15645,504,1389,56};
//     // uint64_t qq[32] = {1950, 1740, 1002, 1564, 604, 1030, 1897, 1528, 290, 1138,
//     //                     1560, 387, 346, 715, 1417, 90, 1036, 1905, 1078, 373,
//     //                     1747, 1034, 1503, 1154, 1282, 217, 685, 1163, 1266, 796,
//     //                     1313, 1486};
//     // qq[0] = 2;
//     for(int i = 0; i < num; ++i){
//         qq[i] = i;
//     }
//     for(int i = num - 1; i >= 1; --i){
//         my_swap(qq[i],qq[rand()%i]);
//     }
//     kv aa;
//     for(int i = 0; i < num; ++i){
//         // scanf("%ld", &aa.key);
//         // aa.key = (i + 1) * 4;
//         aa.key = qq[i];
//         // if(aa.key == 0) break;
//         aa.value = aa.key * 2;
//         printf("insert: %ld\t",aa.key);
//         pmh->insert(aa);
//         // if(i % 64 == 0){
//         //     pmh->display();
//         // }
//         // pmh->display();
//     }
//     for(int i = num - 1; i >= 1; --i){
//         my_swap(qq[i],qq[rand()%i]);
//     }
//     for(int i = 0; i < 1000; ++i){
//         aa.key = qq[i];
//         pmh->remove(aa.key);
//     }
//     pmh->display();
//     // for(int i = 28; i < num/4; ++i){
//     //     // scanf("%ld", &aa.key);
//     //     // aa.key = (i + 1) * 4;
//     //     aa.key = qq[i];
//     //     // if(aa.key == 0) break;
//     //     aa.value = aa.key * 2;
//     //     printf("insert: %ld\t",aa.key);
//     //     pmh->insert(aa);
//     //     // if(i % 64 == 0){
//     //     //     pmh->display();
//     //     // }
//     //     pmh->display();
//     // }

//     // uint64_t vlu = 0;
//     // for(int i = 0; i < 448; ++i){
//     //     aa.key = qq[i];
//     //     // aa.value = i * 12;
//     //     pmh->remove(aa.key);
//     // }
//     // pmh->display();
//     // for(int i = 448; i < 448 + 4; ++i){
//     //     aa.key = qq[i];
//     //     pmh->remove(aa.key);
//     //     pmh->display();
//     // }
//     pmh->selfDestory();
//     // uint64_t new_vlu;
//     // aaaa.key = 60;
//     // aaaa.value = 60 * 2;
//     // pmh->insert(aaaa);
//     // pmh->search(60, new_vlu); // cannot find dest
//     // printf("key: %d, value: %ld\n",60, new_vlu);
//     // pmh->~PmEHash();
// }

int main()
{
    string loadPath[7];
    loadPath[0] = "../workloads/1w-rw-50-50-load.txt";
    loadPath[1] = "../workloads/10w-rw-0-100-load.txt";
    loadPath[2] = "../workloads/10w-rw-25-75-load.txt";
    loadPath[3] = "../workloads/10w-rw-50-50-load.txt";
    loadPath[4] = "../workloads/10w-rw-75-25-load.txt";
    loadPath[5] = "../workloads/10w-rw-100-0-load.txt";
    loadPath[6] = "../workloads/220w-rw-50-50-load.txt";
    string runPath[7];
    runPath[0] = "../workloads/1w-rw-50-50-run.txt";
    runPath[1] = "../workloads/10w-rw-0-100-run.txt";
    runPath[2] = "../workloads/10w-rw-25-75-run.txt";
    runPath[3] = "../workloads/10w-rw-50-50-run.txt";
    runPath[4] = "../workloads/10w-rw-75-25-run.txt";
    runPath[5] = "../workloads/10w-rw-100-0-run.txt";
    runPath[6] = "../workloads/220w-rw-50-50-run.txt";

    vector<string> loadOperation;
    vector<string> runOperation;

    string line;
    for (int file_index = 0; file_index < 7; ++file_index) {
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
        clock_t loadStartTime, loadEndTime;

        loadStartTime = clock();
        PmEHash* ehash = new PmEHash;
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

        loadEndTime = clock();
        cout << "********************************" << loadPath[file_index] << "*******************************\n";
        cout << "load total time : " << (double)(loadEndTime - loadStartTime) / CLOCKS_PER_SEC << "s" << endl;
        cout << "load total operations : " << loadOperation.size() << endl;
        cout << "load operations per second : " << loadOperation.size() * CLOCKS_PER_SEC / (double)(loadEndTime - loadStartTime) << endl;
        
        clock_t runStartTime, runEndTime;
        int INSERT = 0;
        int UPDATE = 0;
        int READ = 0;
        int DELETE = 0;
        int INSERT_SUCCESS = 0;
        int UPDATE_SUCCESS = 0;
        int READ_SUCCESS = 0;
        int DELETE_SUCCESS = 0;
        runStartTime = clock();
        for (int i = 0; i < runOperation.size(); i++) {
            string order = runOperation[i];
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

            // PmEHash* ehash = new PmEHash;
            kv temp;
            temp.key = key;
            temp.value = value;

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
        }

        runEndTime = clock();
        
        cout << "run total Time : " << (double)(runEndTime - runStartTime) / CLOCKS_PER_SEC << "s" << endl;
        cout << "run total operations : " << runOperation.size() << endl;
        cout << "run operations per second : " << runOperation.size() * CLOCKS_PER_SEC / (double)(runEndTime - runStartTime) << endl;
        cout << "INSERT : " << INSERT << " INSERT_SUCCESS : " << INSERT_SUCCESS << endl;
        cout << "UPDATE : " << UPDATE << " UPDATE_SUCCESS : " << UPDATE_SUCCESS << endl;
        cout << "READ : " << READ << " READ_SUCCESS : " << READ_SUCCESS << endl;
        cout << "DELETE : " << DELETE << " DELETE_SUCCESS  : " << DELETE_SUCCESS << endl;
        cout << "********************************" << runPath[file_index] << "*******************************\n";
        ehash->selfDestory();
    }
}
