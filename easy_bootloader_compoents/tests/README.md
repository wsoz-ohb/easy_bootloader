# Host tests

Build and run with MinGW GCC:

```powershell
gcc -std=c99 -Wall -Wextra -Werror -pedantic `
  -I ..\inc -I ..\..\easy_bootloader_common\inc `
  ..\..\easy_bootloader_common\src\boot_image.c `
  ..\..\easy_bootloader_common\src\boot_control.c `
  ..\src\easy_bootloader.c `
  test_easy_bootloader.c `
  -o test_easy_bootloader.exe
.\test_easy_bootloader.exe
```
