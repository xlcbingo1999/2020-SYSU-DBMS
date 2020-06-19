#include"pm_ehash.h"

// 数据页表的相关操作实现都放在这个源文件下，如PmEHash申请新的数据页和删除数据页的底层实现

data_page::data_page(const char* file_name) {
    bitmap = 0;
    size_t map_len;
	int is_pmem;
    for(int i = 0 ; i < DATA_PAGE_SLOT_NUM; ++i) {
        page_bucket[i] = (struct page_inner_bucket *)pmem_map_file(file_name, sizeof(page_inner_bucket), PMEM_FILE_CREATE, 0777, &map_len, &is_pmem);
    }
}