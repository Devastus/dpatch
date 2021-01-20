EXECUTABLE = dpatch

SRC_DIR = src
BUILD_DIR = obj
TARGET_DIR = bin
TESTS_DIR = tests

CC = gcc
CFLAGS = -Wall
LDFLAGS =
DEFINES = -DLOG_LEVEL=4

INCLUDE_DIRS = $(SRC_DIR)
INCLUDES = $(addprefix -I, $(INCLUDE_DIRS)) -Ilib

SRCEXT = c
SOURCES = $(shell find $(SRC_DIR) -type f -name *.$(SRCEXT))
OBJECTS = $(patsubst $(SRC_DIR)/%, $(BUILD_DIR)/%, $(SOURCES:.$(SRCEXT)=.o))
TESTS = $(shell find $(TESTS_DIR) -type f -name *.$(SRCEXT))

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	@mkdir -p $(TARGET_DIR)
	$(CC) $^ -o  $(TARGET_DIR)/$(EXECUTABLE) $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.$(SRCEXT)
	@mkdir -p $(dir $@)
	$(CC) -g $(CFLAGS) $(DEFINES) $(INCLUDES) -c $< -o $@

print-%:
	@echo $*=$($*)

.PHONY: clean

clean:
	@echo " Cleaning..."
	@echo " $(RM) -r $(BUILD_DIR) $(TARGET_DIR)"; $(RM) -r $(BUILD_DIR) $(TARGET_DIR)

test:
	@echo "Compiling and running tests..."
	rm -f $(TESTS_DIR)/test
	$(CC) $(TESTS_DIR)/main.c -g $(CFLAGS) $(INCLUDES) -DLOG_LEVEL=0 -Itests -o $(TESTS_DIR)/test
	./$(TESTS_DIR)/test
	rm -f $(TESTS_DIR)/test
