BUILD ?= build

ifeq ($(abspath $(BUILD)),$(shell pwd)) 
$(error "ERROR: Build dir can't be equal to source dir")
endif

NVRAM_SRC_VERSION := $(shell git describe --dirty --always --tags)
CFLAGS += -DSRC_VERSION=$(NVRAM_SRC_VERSION)

CFLAGS += -std=gnu11 -Wall -Wextra -Werror -pedantic
ifeq ($(NVRAM_USE_SANITIZER), 1)
	CFLAGS += -fsanitize=address -fsanitize=undefined
	LDFLAGS += -fsanitize=address -fsanitize=undefined
endif
CLANG_TIDY_CHECKS_LIST = -*
CLANG_TIDY_CHECKS_LIST += clang-analyzer-*
CLANG_TIDY_CHECKS_LIST += bugprone-*
CLANG_TIDY_CHECKS_LIST += cppcoreguidelines-*
CLANG_TIDY_CHECKS_LIST += portability-*
CLANG_TIDY_CHECKS_LIST += readability-*
CLANG_TIDY_CHECKS_LIST += -readability-braces-around-statements
CLANG_TIDY_CHECKS_LIST += -clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling
CLANG_TIDY_CHECKS_LIST += -bugprone-easily-swappable-parameters
space := $() $()
comma := ,
CLANG_TIDY_CHECKS ?= $(subst $(space),$(comma),$(CLANG_TIDY_CHECKS_LIST))

# Setting NVRAM_SYSTEM_PREFIX to empty value
# will disable the prefix enforcement.
NVRAM_SYSTEM_PREFIX ?= SYS_
CFLAGS += -DNVRAM_SYSTEM_PREFIX=$(NVRAM_SYSTEM_PREFIX)

NVRAM_INTERFACE_DEFAULT ?= file
NVRAM_INTERFACE_FILE ?= 1
NVRAM_INTERFACE_MTD ?= 0
NVRAM_INTERFACE_EFI ?= 0
CFLAGS += -DNVRAM_INTERFACE_DEFAULT=$(NVRAM_INTERFACE_DEFAULT)
CFLAGS += -DNVRAM_INTERFACE_EFI=$(NVRAM_INTERFACE_EFI)
CFLAGS += -DNVRAM_INTERFACE_MTD=$(NVRAM_INTERFACE_MTD)
CFLAGS += -DNVRAM_INTERFACE_FILE=$(NVRAM_INTERFACE_FILE)

NVRAM_FORMAT_DEFAULT ?= v2
NVRAM_FORMAT_V2 ?= 1
NVRAM_FORMAT_LEGACY ?= 0
CFLAGS += -DNVRAM_FORMAT_DEFAULT=$(NVRAM_FORMAT_DEFAULT)
CFLAGS += -DNVRAM_FORMAT_V2=$(NVRAM_FORMAT_V2)
CFLAGS += -DNVRAM_FORMAT_LEGACY=$(NVRAM_FORMAT_LEGACY)
OBJS = log.o main.o nvram_format.o nvram_interface.o libnvram/libnvram.a

ifeq ($(NVRAM_INTERFACE_FILE), 1)
OBJS += nvram_interface_file.o
NVRAM_FILE_SYSTEM_A ?= /var/nvram/system_a
NVRAM_FILE_SYSTEM_B ?= /var/nvram/system_b
NVRAM_FILE_USER_A ?= /var/nvram/user_a
NVRAM_FILE_USER_B ?= /var/nvram/user_b
CFLAGS += -DNVRAM_FILE_SYSTEM_A=$(NVRAM_FILE_SYSTEM_A)
CFLAGS += -DNVRAM_FILE_SYSTEM_B=$(NVRAM_FILE_SYSTEM_B)
CFLAGS += -DNVRAM_FILE_USER_A=$(NVRAM_FILE_USER_A)
CFLAGS += -DNVRAM_FILE_USER_B=$(NVRAM_FILE_USER_B)
endif

ifeq ($(NVRAM_INTERFACE_MTD), 1)
OBJS += nvram_interface_mtd.o
LDFLAGS += -lmtd
NVRAM_MTD_SYSTEM_A ?= system_a
NVRAM_MTD_SYSTEM_B ?= system_b
NVRAM_MTD_USER_A ?= user_a
NVRAM_MTD_USER_B ?= user_b
CFLAGS += -DNVRAM_MTD_SYSTEM_A=$(NVRAM_MTD_SYSTEM_A)
CFLAGS += -DNVRAM_MTD_SYSTEM_B=$(NVRAM_MTD_SYSTEM_B)
CFLAGS += -DNVRAM_MTD_USER_A=$(NVRAM_MTD_USER_A)
CFLAGS += -DNVRAM_MTD_USER_B=$(NVRAM_MTD_USER_B)
endif

ifeq ($(NVRAM_INTERFACE_EFI), 1)
OBJS += nvram_interface_efi.o
LDFLAGS += -le2p
NVRAM_EFI_SYSTEM_A ?= /sys/firmware/efi/efivars/604dafe4-587a-47f6-8604-3d33eb83da3d-system
NVRAM_EFI_SYSTEM_B ?= 
NVRAM_EFI_USER_A ?= /sys/firmware/efi/efivars/604dafe4-587a-47f6-8604-3d33eb83da3d-user
NVRAM_EFI_USER_B ?= 
CFLAGS += -DNVRAM_EFI_SYSTEM_A=$(NVRAM_EFI_SYSTEM_A)
CFLAGS += -DNVRAM_EFI_SYSTEM_B=$(NVRAM_EFI_SYSTEM_B)
CFLAGS += -DNVRAM_EFI_USER_A=$(NVRAM_EFI_USER_A)
CFLAGS += -DNVRAM_EFI_USER_B=$(NVRAM_EFI_USER_B)
endif

ifeq ($(NVRAM_FORMAT_V2), 1)
OBJS += nvram_format_v2.o
endif

ifeq ($(NVRAM_FORMAT_LEGACY), 1)
OBJS += nvram_format_legacy.o
endif

all: nvram
.PHONY : all

.PHONY: nvram
nvram: $(BUILD)/nvram

$(BUILD)/nvram: $(addprefix $(BUILD)/, $(OBJS))
	$(CC) -o $@ $^ $(LDFLAGS)

$(BUILD)/%.o: %.c 
ifeq ($(NVRAM_CLANG_TIDY), 1)
	clang-tidy $< -header-filter=.* \
		-checks=$(CLANG_TIDY_CHECKS) -- $<
endif
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/libnvram/libnvram.a:
	make -C libnvram CLANG_TIDY=no BUILD=$(abspath $(BUILD)/libnvram/)

clean:
	rm -rf $(BUILD)
