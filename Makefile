
CC ?= gcc
INSTALL_PATH ?= /usr/sbin

NVRAM_INTERFACE_TYPE ?= file
OBJS = log.o nvram.o main.o libnvram/libnvram.a

NVRAM_ALLOW_ALL_PREFIXES ?= no
NVRAM_VALID_ATTRIBUTES ?= none
NVRAM_INIT_ENABLED ?= no

NVRAM_SRC_VERSION := $(shell git describe --dirty --always --tags)
ifeq ($(NVRAM_INTERFACE_TYPE), file)
OBJS += nvram_interface_file.o
NVRAM_SYSTEM_A ?= /srv/nvram/system_a
NVRAM_SYSTEM_B ?= /srv/nvram/system_b
NVRAM_USER_A ?= /srv/nvram/user_a
NVRAM_USER_B ?= /srv/nvram/user_b
endif

ifeq ($(NVRAM_INTERFACE_TYPE), mtd)
OBJS += nvram_interface_mtd.o
LDFLAGS += -lmtd
NVRAM_SYSTEM_A ?= system_a
NVRAM_SYSTEM_B ?= system_b
NVRAM_USER_A ?= user_a
NVRAM_USER_B ?= user_b
endif

ifeq ($(NVRAM_INTERFACE_TYPE), efi)
OBJS += nvram_interface_efi.o
LDFLAGS += -le2p
NVRAM_SYSTEM_A ?= /sys/firmware/efi/efivars/604dafe4-587a-47f6-8604-3d33eb83da3d-system
NVRAM_USER_A ?= /sys/firmware/efi/efivars/604dafe4-587a-47f6-8604-3d33eb83da3d-user
endif

CFLAGS += -DNVRAM_ALLOW_ALL_PREFIXES=$(NVRAM_ALLOW_ALL_PREFIXES)
CFLAGS += -DVALID_ATTRIBUTES=$(NVRAM_VALID_ATTRIBUTES)

CFLAGS += -std=gnu11 -Wall -Wextra -Werror -pedantic
CFLAGS += -DNVRAM_SYSTEM_A=$(NVRAM_SYSTEM_A)
CFLAGS += -DNVRAM_SYSTEM_B=$(NVRAM_SYSTEM_B)
CFLAGS += -DNVRAM_USER_A=$(NVRAM_USER_A)
CFLAGS += -DNVRAM_USER_B=$(NVRAM_USER_B)
CFLAGS += -DSRC_VERSION=$(NVRAM_SRC_VERSION)
CFLAGS += -DINTERFACE_TYPE=$(NVRAM_INTERFACE_TYPE)
CFLAGS += -DNVRAM_INIT_ENABLED=$(NVRAM_INIT_ENABLED)

all: nvram
.PHONY : all

nvram : $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: libnvram/libnvram.a
libnvram/libnvram.a:
	make -C libnvram CLANG_TIDY=no

install:
	install -m 0755 -D nvram $(INSTALL_PATH)/

clean:
	rm -f *.o
	rm -f nvram
	make -C libnvram clean
