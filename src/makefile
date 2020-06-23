# 编译加了libpmem库的源文件时加上-l pmem参数进行编译
# 在这个makefile中添加ycsb测试的相关编译命令，运行ycsb

# Where to find user code.
CC := g++
FLAGS := -g
SRC_DIR := .
INC_DIR := ../include
BUILD_DIR := ./build
BIN_DIR := ./bin
INCLUDE := -I./$(INC_DIR)

$(BIN_DIR)/ycsb: $(BUILD_DIR)/data_page.o $(BUILD_DIR)/pm_ehash.o $(BUILD_DIR)/ycsb.o
	@mkdir -p $(BIN_DIR)
	$(CC) $(FLAGS) $(INCLUDE) $^ -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	$(CC) $(FLAGS) $(INCLUDE) -c -o $@ $<

clean:
	@rm -rf $(BUILD_DIR)
	@rm -rf $(BIN_DIR)

