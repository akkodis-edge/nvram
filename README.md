# nvram
Utility for operating on platform key-value pairs. Various interfaces and serialization formats available.

# interfaces

Default paths for interfaces are compiled in and are modifiable by environment variables in the format:

NVRAM_${INTERFACE}_USER_A

As an example modifying FILE interface path for system B partition to /tmp/system_b:

NVRAM_FILE_SYSTEM_B=/tmp/system_b

** file **

file or file like objects.

** mtd **

mtd devices, typically spi-nor.

** efi **

UEFI variable storage.

# formats
** legacy **

Not recommended. Stores strings in text mode files.

Only single section (A) supported.

Newline separated entries consisting of:

KEY1=VALUE1\n

KEY2=VALUE2\n

** v2 **

Binary format serialized by libnvram v2. See libnvram/libnvram.h for details.

Supports A/B sections with power fail safe updates.

# Build
Compiled in formats and interfaces are controlled by flags to make.

Available flags and default values described here:

**interfaces:**

NVRAM_INTERFACE_DEFAULT=file

NVRAM_INTERFACE_FILE=1

NVRAM_FILE_SYSTEM_A=/var/nvram/system_a

NVRAM_FILE_SYSTEM_B=/var/nvram/system_b

NVRAM_FILE_USER_A=/var/nvram/user_a

NVRAM_FILE_USER_B=/var/nvram/user_b

NVRAM_INTERFACE_MTD=0

NVRAM_MTD_SYSTEM_A=system_a

NVRAM_MTD_SYSTEM_B=system_b

NVRAM_MTD_USER_A=user_a

NVRAM_MTD_USER_B=user_b

NVRAM_INTERFACE_EFI=0

NVRAM_EFI_SYSTEM_A=/sys/firmware/efi/efivars/604dafe4-587a-47f6-8604-3d33eb83da3d-system

NVRAM_EFI_SYSTEM_B="" 

NVRAM_EFI_USER_A=/sys/firmware/efi/efivars/604dafe4-587a-47f6-8604-3d33eb83da3d-user

NVRAM_EFI_USER_B="" 

**formats:**

NVRAM_FORMAT_DEFAULT=v2

NVRAM_FORMAT_V2=1

NVRAM_FORMAT_LEGACY=0

## Testing
Build:

``` 
make clean
make NVRAM_USE_SANITIZER=1 NVRAM_FORMAT_LEGACY=1 NVRAM_CLANG_TIDY=1
```

Run tests:

```
./test.py
```
