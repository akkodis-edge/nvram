INSTALL_PATH ?= /usr/sbin
BUILD ?= build

ifeq ($(abspath $(BUILD)),$(shell pwd)) 
$(error "ERROR: Build dir can't be equal to source dir")
endif

NVRAM_SRC_VERSION := $(shell git describe --dirty --always --tags)
CFLAGS += -DSRC_VERSION=$(NVRAM_SRC_VERSION)

CFLAGS += -std=gnu11 -Wall -Wextra -Werror -pedantic
ifeq ($(NVRAM_USE_SANITIZER), ON)
	CFLAGS += -fsanitize=address -fsanitize=undefined
	LDFLAGS += -fsanitize=address -fsanitize=undefined
endif

NVRAM_INTERFACE_DEFAULT ?= file
NVRAM_INTERFACE_FILE ?= ON
CFLAGS += -DNVRAM_INTERFACE_DEFAULT=$(NVRAM_INTERFACE_DEFAULT)
CFLAGS += -DNVRAM_INTERFACE_FILE=$(NVRAM_INTERFACE_FILE)

OBJS = log.o nvram.o main.o nvram_interface.o libnvram/libnvram.a

ifeq ($(NVRAM_INTERFACE_FILE), ON)
OBJS += nvram_interface_file.o
NVRAM_FILE_SYSTEM_A ?= /home/lelle/nvram/system_a
NVRAM_FILE_SYSTEM_B ?= 
NVRAM_FILE_USER_A ?= /home/lelle/nvram/user_a
NVRAM_FILE_USER_B ?= 
CFLAGS += -DNVRAM_FILE_SYSTEM_A=$(NVRAM_FILE_SYSTEM_A)
CFLAGS += -DNVRAM_FILE_SYSTEM_B=$(NVRAM_FILE_SYSTEM_B)
CFLAGS += -DNVRAM_FILE_USER_A=$(NVRAM_FILE_USER_A)
CFLAGS += -DNVRAM_FILE_USER_B=$(NVRAM_FILE_USER_B)
endif

#ifeq ($(NVRAM_INTERFACE_TYPE), mtd)
#OBJS += nvram_interface_mtd.o
#LDFLAGS += -lmtd
#NVRAM_SYSTEM_A ?= system_a
#NVRAM_SYSTEM_B ?= system_b
#NVRAM_USER_A ?= user_a
#NVRAM_USER_B ?= user_b
#endif

#ifeq ($(NVRAM_INTERFACE_TYPE), efi)
#OBJS += nvram_interface_efi.o
#LDFLAGS += -le2p
#NVRAM_SYSTEM_A ?= /sys/firmware/efi/efivars/604dafe4-587a-47f6-8604-3d33eb83da3d-system
#NVRAM_USER_A ?= /sys/firmware/efi/efivars/604dafe4-587a-47f6-8604-3d33eb83da3d-user
#endif


#CFLAGS += -DNVRAM_SYSTEM_A=$(NVRAM_SYSTEM_A)
#CFLAGS += -DNVRAM_SYSTEM_B=$(NVRAM_SYSTEM_B)
#CFLAGS += -DNVRAM_USER_A=$(NVRAM_USER_A)
#CFLAGS += -DNVRAM_USER_B=$(NVRAM_USER_B)



all: nvram
.PHONY : all

.PHONY: nvram
nvram: $(BUILD)/nvram

$(BUILD)/nvram: $(addprefix $(BUILD)/, $(OBJS)) $(BUILD)/libnvram/libnvram.a
	$(CC) -o $@ $^ $(LDFLAGS)

$(BUILD)/%.o: %.c 
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/libnvram/libnvram.a:
	make -C libnvram CLANG_TIDY=no BUILD=$(abspath $(BUILD)/libnvram/)

clean:
	rm -r $(BUILD)
