# APP download component porting

The APP component is transport- and storage-independent. It does not erase or
program the MCU application area. External `fw_a` and `fw_b` are symmetric
firmware slots. The APP always downloads to the slot opposite the currently
confirmed slot.

## Required bindings

- `transport_read` / `transport_write`: bind to the upgrade UART ring buffer.
- `read_boot_control`: read the latest valid BCB record. Only `EMPTY` and
  `CONFIRMED` allow a new download.
- `storage_erase` / `storage_write` / `storage_read`: route `BOOT_SLOT_A` to
  FAL `fw_a` and `BOOT_SLOT_B` to FAL `fw_b`, using relative offsets.
- `bcb_read` / `bcb_program` / `bcb_erase`: expose the same internal Sector 3
  BCB storage used by Bootloader. They are used to reject an update before the
  BCB no longer has enough records for a complete install/trial/rollback flow.
- `mark_update_ready`: append `UPDATE_READY` with the selected pending slot,
  payload size, CRC32 and version.
- `mark_confirmed`: append `CONFIRMED` after `easy_bootloader_app_confirm_running()`.
- `system_reset`: reset after the image and BCB record are committed.

For RT-Thread FAL, the slot storage callbacks are thin wrappers around:

```c
const struct fal_partition *part = slot == BOOT_SLOT_A ? fw_a : fw_b;
fal_partition_erase(part, offset, length);
fal_partition_write(part, offset, data, length);
fal_partition_read(part, offset, data, length);
```

The component owns the first 64 bytes of each slot as an image header. Firmware
payload starts at offset 64. The header commit marker is written last.

Slot selection is:

```text
confirmed A -> download B
confirmed B -> download A
no confirmed external slot -> download A
```

For the first upgrade, Bootloader must back up the running internal image into
slot B before erasing internal Application Flash. After a trial image is
confirmed, its source slot becomes the new confirmed slot.

## Wire protocol compatibility

The existing protocol is retained:

- query version: `55 AA FF DD 55 55`
- query date: `55 AA FF CC 55 55`
- start update: `55 AA FF EE 55 55`
- data frames and per-frame ACK are unchanged
- finish frame remains `version + date + FF FD`

The semantic change is intentional: `start update` now downloads into the
inactive external slot selected from BCB state; it no longer erases an internal
Flash flag and immediately resets.
