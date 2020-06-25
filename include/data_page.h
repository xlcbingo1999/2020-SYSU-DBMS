#ifndef DATA_PAGE
#define DATA_PAGE
#include <cstdint>
#include <libpmem.h>
#define DATA_PAGE_SLOT_NUM 16

// use pm_address to locate the data in the page


// use pm_address to locate the data in the page

typedef struct catalog_file_item {
    uint32_t fileId;
    uint32_t offset;
} catalog_file_item;

typedef struct catalog_page_file {
    catalog_file_item catalog_item[512];
} catalog_page_file;

typedef struct bucket_inner_kv {
    uint64_t key;
    uint64_t value;
} bucket_inner_kv;

typedef struct page_inner_bucket {
    // 32bytes
    uint8_t bitmap[2];
    bucket_inner_kv inner_kv[15];
    uint8_t unused_byte_in_bucket[13];
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

#endif