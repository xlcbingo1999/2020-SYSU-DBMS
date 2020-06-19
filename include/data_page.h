#ifndef DATA_PAGE
#define DATA_PAGE
#include <libpmem.h>
#define DATA_PAGE_SLOT_NUM 16

// use pm_address to locate the data in the page

typedef struct page_inner_bucket {
    // 32bytes
    uint8_t mem[255];
} page_inner_bucket;

// uncompressed page format design to store the buckets of PmEHash
// one slot stores one bucket of PmEHash
typedef struct data_page {
    // fixed-size record design
    // uncompressed page format
    uint16_t bitmap;
    uint8_t unused_byte_in_data_page[14];
    struct page_inner_bucket *page_bucket[DATA_PAGE_SLOT_NUM];
    data_page(const char* file_name);
} data_page;

#endif