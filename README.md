# nvram
Utility for operating on platform key-value pairs. Various interfaces and serialization formats available.

# interfaces
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

Supports A/B sections.

# Build
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
