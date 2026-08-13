# Bootloader core

This component installs an already verified external image into the MCU App
region. It does not parse the upgrade UART protocol and does not download a
firmware image itself.

## State flow

```text
CONFIRMED
  -> App writes UPDATE_READY after staging fw_a or fw_b
  -> Bootloader verifies pending slot
  -> INSTALLING
  -> TRIAL
  -> App confirms itself as CONFIRMED

TRIAL reset limit reached
  -> ROLLBACK
  -> restore confirmed slot
  -> CONFIRMED
```

The two external slots are symmetric. A confirmed A image is followed by a
download to B, and vice versa. During the first update, the Bootloader copies
the current internal App to the opposite slot before erasing internal Flash.

`boot_control.*` implements 64-byte append-only BCB records. The record CRC is
checked during startup and the commit marker is programmed last. The BCB sector
is recycled only while the internal App and its confirmed external image are
both verified.

The STM32F407 reference port uses:

```text
Bootloader: 0x08000000 - 0x0800BFFF  (48 KiB)
BCB:        0x0800C000 - 0x0800FFFF  (16 KiB)
App:        0x08010000 - 0x080FFFFF  (960 KiB)
```

The matching host test is in `tests/`.
