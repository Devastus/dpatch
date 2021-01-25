EXECUTABLE = dpatch

SRC_DIR = src
BUILD_DIR = obj
TARGET_DIR = bin
TESTS_DIR = tests

CC = gcc
CFLAGS = -std=c11 -Wall
LDFLAGS =
DEFINES = -DLOG_LEVEL=1 -DSERVER_DEBUG

INCLUDE_DIRS = $(SRC_DIR)
INCLUDES = $(addprefix -I, $(INCLUDE_DIRS)) -Ilib

SRCEXT = c
SOURCES = $(shell find $(SRC_DIR) -type f -name *.$(SRCEXT))
OBJECTS = $(patsubst $(SRC_DIR)/%, $(BUILD_DIR)/%, $(SOURCES:.$(SRCEXT)=.o))
TESTS = $(shell find $(TESTS_DIR) -type f -name *.$(SRCEXT))

PREFIX = /usr/local

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	@mkdir -p $(TARGET_DIR)
	$(CC) $^ -o  $(TARGET_DIR)/$(EXECUTABLE) $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.$(SRCEXT)
	@mkdir -p $(dir $@)
	$(CC) -g $(CFLAGS) $(DEFINES) $(INCLUDES) -c $< -o $@

print-%:
	@echo $*=$($*)

clean:
	@echo " Cleaning..."
	@echo " $(RM) -r $(BUILD_DIR) $(TARGET_DIR)"; $(RM) -r $(BUILD_DIR) $(TARGET_DIR)

install: clean all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f ${TARGET_DIR}/${EXECUTABLE} ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/${EXECUTABLE}

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/${EXECUTABLE}

test:
	@echo "Compiling and running tests..."
	rm -f $(TESTS_DIR)/test
	$(CC) $(TESTS_DIR)/main.c -g $(CFLAGS) $(INCLUDES) -DLOG_LEVEL=0 -Itests -o $(TESTS_DIR)/test
	./$(TESTS_DIR)/test
	rm -f $(TESTS_DIR)/test

.PHONY: all clean test install uninstall
