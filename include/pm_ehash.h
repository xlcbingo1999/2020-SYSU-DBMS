#ifndef _PM_E_HASH_H
#define _PM_E_HASH_H

#include "data_page.h"

#include <iterator>
#include <map>
#include <queue>
#include <string>
#include <set>
#include <libpmem.h>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdlib.h>

#define BUCKET_SLOT_NUM 15
#define DEFAULT_CATALOG_SIZE 16
#define META_NAME "pm_ehash_metadata";
#define CATALOG_NAME "pm_ehash_catalog";
#define PM_EHASH_DIRECTORY "/home/xlc/Desktop/2020-SYSU-DBMS/data/"; // add your own directory path to store the pm_ehash
#define FILE_NAME "file_name";

using std::map;
using std::queue;
using std::set;

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
    catalog_page_file** catalog_file_table;

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

#endif
