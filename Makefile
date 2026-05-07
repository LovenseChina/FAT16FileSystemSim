CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g
LDFLAGS = 
SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj

FAT16_SRC = $(SRC_DIR)/FAT16.cpp $(SRC_DIR)/BlockDevice.cpp
FAT16_OBJ = $(OBJ_DIR)/FAT16.o $(OBJ_DIR)/BlockDevice.o

# Test executables
TESTS = \
	cluster_test \
	entry_test \
	path_test \
	validation_test \
	file_ops_test \
	file_rw_test \
	dir_test \
	full_link_test \
	FAT16_path_resolve_test \
	FAT16_constructor_test

.PHONY: all clean test

all: $(TESTS)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/FAT16.o: $(SRC_DIR)/FAT16.cpp $(INC_DIR)/FAT16.hpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -c $< -o $@

$(OBJ_DIR)/BlockDevice.o: $(SRC_DIR)/BlockDevice.cpp $(INC_DIR)/BlockDevice.hpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -c $< -o $@

# Pattern rule: each test .cpp links with FAT16_OBJ
%_test: $(SRC_DIR)/%_test.cpp $(FAT16_OBJ)
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) $< $(FAT16_OBJ) -o $@

FAT16_path_resolve_test: $(SRC_DIR)/FAT16_path_resolve_test.cpp $(FAT16_OBJ)
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) $< $(FAT16_OBJ) -o $@

FAT16_constructor_test: $(SRC_DIR)/FAT16_constructor_test.cpp $(FAT16_OBJ)
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) $< $(FAT16_OBJ) -o $@

# Run all tests and report failures
test: $(TESTS)
	@echo "=================================================="
	@echo "  Running all tests..."
	@echo "=================================================="
	@failed=0; \
	for t in $(TESTS); do \
		echo "--- $$t ---"; \
		./$$t && echo "PASS" || { echo "FAIL"; failed=1; }; \
		echo; \
	done; \
	if [ $$failed -eq 0 ]; then \
		echo "All tests passed!"; \
	else \
		echo "Some tests FAILED!"; \
	fi

# Quick single test
test_%: %_test
	@echo "--- $*_test ---"
	./$*_test && echo "PASS" || echo "FAIL"

clean:
	rm -f $(TESTS) $(OBJ_DIR)/*.o
	rmdir $(OBJ_DIR) 2>/dev/null || true
