# NVRAM

## Build
**Build with valid configuration attributes**
- make NVRAM_VALID_ATTRIBUTES=LM_PRODUCT_DATE:LM_PRODUCT_ID 

## Examples
**Init with config file**
- NVRAM_SYSTEM_UNLOCK=16440 nvram --init config_file

**Set attribute on system partition**
- NVRAM_SYSTEM_UNLOCK=16440 nvram --sys --set SYS_PRODUCT_ID 12345

**Set attribute on user partition**
- nvram --set LM_PRODUCT_ID 1234

**Get attribute value from system partition**
- nvram --get SYS_BT_PID

**Get attribute value from user partition**
- nvram --get LM_PRODUCT_ID

**Get attribute list from user and system partition**
- nvram --list

**Get attribute list from system partition**
- nvram --sys --list

