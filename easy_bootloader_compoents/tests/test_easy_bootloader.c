#include "easy_bootloader.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define TEST_BCB_SIZE       (BOOT_CONTROL_RECORD_SIZE * 16U)
#define TEST_SLOT_SIZE      4096U
#define TEST_APP_SIZE       2048U

typedef struct
{
    uint8_t bcb[TEST_BCB_SIZE];
    uint8_t slot_a[TEST_SLOT_SIZE];
    uint8_t slot_b[TEST_SLOT_SIZE];
    uint8_t app[TEST_APP_SIZE];
    uint32_t jump_count;
    uint32_t app_erase_count;
    uint8_t interrupt_after_erase;
    uint8_t fail_external_erase;
} test_context_t;

static bool range_valid(uint32_t offset, uint32_t length, uint32_t capacity)
{
    return (offset <= capacity) && (length <= (capacity - offset));
}

static boot_loader_status_t bcb_read(void *context,
                                     uint32_t offset,
                                     uint8_t *data,
                                     uint32_t length)
{
    test_context_t *test = context;
    if (!range_valid(offset, length, sizeof(test->bcb)))
    {
        return BOOT_LOADER_IO_ERROR;
    }
    memcpy(data, &test->bcb[offset], length);
    return BOOT_LOADER_OK;
}

static boot_loader_status_t bcb_program(void *context,
                                        uint32_t offset,
                                        const uint8_t *data,
                                        uint32_t length)
{
    test_context_t *test = context;
    uint32_t index;

    if (!range_valid(offset, length, sizeof(test->bcb)))
    {
        return BOOT_LOADER_IO_ERROR;
    }
    for (index = 0U; index < length; index++)
    {
        if ((test->bcb[offset + index] & data[index]) != data[index])
        {
            return BOOT_LOADER_IO_ERROR;
        }
        test->bcb[offset + index] &= data[index];
    }
    return BOOT_LOADER_OK;
}

static boot_loader_status_t bcb_erase(void *context,
                                      uint32_t offset,
                                      uint32_t length)
{
    test_context_t *test = context;
    if (!range_valid(offset, length, sizeof(test->bcb)))
    {
        return BOOT_LOADER_IO_ERROR;
    }
    memset(&test->bcb[offset], 0xFF, length);
    return BOOT_LOADER_OK;
}

static int bcb_read_storage(void *context,
                            uint32_t offset,
                            uint8_t *data,
                            uint32_t length)
{
    return (bcb_read(context, offset, data, length) == BOOT_LOADER_OK) ? 0 : -1;
}

static int bcb_program_storage(void *context,
                               uint32_t offset,
                               const uint8_t *data,
                               uint32_t length)
{
    return (bcb_program(context, offset, data, length) == BOOT_LOADER_OK) ? 0 : -1;
}

static int bcb_erase_storage(void *context, uint32_t offset, uint32_t length)
{
    return (bcb_erase(context, offset, length) == BOOT_LOADER_OK) ? 0 : -1;
}

static uint8_t *slot_data(test_context_t *test, boot_slot_t slot)
{
    if (slot == BOOT_SLOT_A)
    {
        return test->slot_a;
    }
    if (slot == BOOT_SLOT_B)
    {
        return test->slot_b;
    }
    return NULL;
}

static boot_loader_status_t external_read(void *context,
                                          boot_slot_t slot,
                                          uint32_t offset,
                                          uint8_t *data,
                                          uint32_t length)
{
    test_context_t *test = context;
    uint8_t *storage = slot_data(test, slot);
    if ((storage == NULL) || !range_valid(offset, length, TEST_SLOT_SIZE))
    {
        return BOOT_LOADER_IO_ERROR;
    }
    memcpy(data, &storage[offset], length);
    return BOOT_LOADER_OK;
}

static boot_loader_status_t external_erase(void *context,
                                           boot_slot_t slot,
                                           uint32_t offset,
                                           uint32_t length)
{
    test_context_t *test = context;
    uint8_t *storage = slot_data(test, slot);
    if ((storage == NULL) || !range_valid(offset, length, TEST_SLOT_SIZE))
    {
        return BOOT_LOADER_IO_ERROR;
    }
    if (test->fail_external_erase != 0U)
    {
        return BOOT_LOADER_IO_ERROR;
    }
    memset(&storage[offset], 0xFF, length);
    return BOOT_LOADER_OK;
}

static boot_loader_status_t external_write(void *context,
                                           boot_slot_t slot,
                                           uint32_t offset,
                                           const uint8_t *data,
                                           uint32_t length)
{
    test_context_t *test = context;
    uint8_t *storage = slot_data(test, slot);
    uint32_t index;

    if ((storage == NULL) || !range_valid(offset, length, TEST_SLOT_SIZE))
    {
        return BOOT_LOADER_IO_ERROR;
    }
    for (index = 0U; index < length; index++)
    {
        if ((storage[offset + index] & data[index]) != data[index])
        {
            return BOOT_LOADER_IO_ERROR;
        }
        storage[offset + index] &= data[index];
    }
    return BOOT_LOADER_OK;
}

static boot_loader_status_t app_erase(void *context,
                                      uint32_t offset,
                                      uint32_t length)
{
    test_context_t *test = context;
    if (!range_valid(offset, length, TEST_APP_SIZE))
    {
        return BOOT_LOADER_IO_ERROR;
    }
    test->app_erase_count++;
    memset(&test->app[offset], 0xFF, length);
    return BOOT_LOADER_OK;
}

static boot_loader_status_t app_write(void *context,
                                      uint32_t offset,
                                      const uint8_t *data,
                                      uint32_t length)
{
    test_context_t *test = context;
    uint32_t index;
    if (!range_valid(offset, length, TEST_APP_SIZE) ||
        (test->interrupt_after_erase != 0U))
    {
        return BOOT_LOADER_IO_ERROR;
    }
    for (index = 0U; index < length; index++)
    {
        if ((test->app[offset + index] & data[index]) != data[index])
        {
            return BOOT_LOADER_IO_ERROR;
        }
        test->app[offset + index] &= data[index];
    }
    return BOOT_LOADER_OK;
}

static boot_loader_status_t app_read(void *context,
                                     uint32_t offset,
                                     uint8_t *data,
                                     uint32_t length)
{
    test_context_t *test = context;
    if (!range_valid(offset, length, TEST_APP_SIZE))
    {
        return BOOT_LOADER_IO_ERROR;
    }
    memcpy(data, &test->app[offset], length);
    return BOOT_LOADER_OK;
}

static void jump_to_app(void *context, uint32_t app_address)
{
    test_context_t *test = context;
    assert(app_address == BOOT_APP_START_ADDR);
    test->jump_count++;
}

static const boot_loader_ops_t test_ops_template =
{
    NULL,
    bcb_read,
    bcb_program,
    bcb_erase,
    external_read,
    external_erase,
    external_write,
    app_erase,
    app_write,
    app_read,
    NULL,
    jump_to_app,
    NULL,
};

static void initialize_test(test_context_t *test)
{
    memset(test, 0, sizeof(*test));
    memset(test->bcb, 0xFF, sizeof(test->bcb));
    memset(test->slot_a, 0xFF, sizeof(test->slot_a));
    memset(test->slot_b, 0xFF, sizeof(test->slot_b));
    memset(test->app, 0xFF, sizeof(test->app));
}

static void prepare_image(uint8_t *slot,
                          const uint8_t *payload,
                          uint32_t payload_size,
                          uint32_t version)
{
    boot_image_info_t image;

    memset(&image, 0, sizeof(image));
    image.target_address = BOOT_APP_START_ADDR;
    image.firmware_version = version;
    image.build_date = 0x20260812UL;
    image.payload_offset = BOOT_IMAGE_PAYLOAD_OFFSET;
    image.payload_size = payload_size;
    image.payload_crc32 = boot_crc32_finish(
        boot_crc32_update(0xFFFFFFFFUL, payload, payload_size));
    boot_image_header_encode(&image, slot);
    memcpy(&slot[BOOT_IMAGE_PAYLOAD_OFFSET], payload, payload_size);
}

static boot_loader_config_t test_config(void)
{
    boot_loader_config_t config;

    easy_bootloader_get_default_config(&config);
    config.app_max_size = TEST_APP_SIZE;
    config.bcb_region_size = TEST_BCB_SIZE;
    config.slot_a_size = TEST_SLOT_SIZE;
    config.slot_b_size = TEST_SLOT_SIZE;
    config.external_erase_size = 64U;
    config.max_boot_attempts = 2U;
    return config;
}

static void append_bcb(test_context_t *test, const boot_control_status_t *status)
{
    boot_control_storage_t storage;

    storage.context = test;
    storage.read = bcb_read_storage;
    storage.program = bcb_program_storage;
    storage.erase = bcb_erase_storage;
    storage.region_size = TEST_BCB_SIZE;
    assert(boot_control_append(&storage, status));
}

static void test_update_installs_and_enters_trial(void)
{
    uint8_t old_payload[160];
    uint8_t new_payload[220];
    boot_control_status_t control;
    boot_loader_ops_t ops = test_ops_template;
    boot_loader_progress_t progress;
    boot_loader_config_t config = test_config();
    test_context_t test;
    uint32_t index;

    initialize_test(&test);
    for (index = 0U; index < sizeof(old_payload); index++)
    {
        old_payload[index] = (uint8_t)(index + 3U);
    }
    for (index = 0U; index < sizeof(new_payload); index++)
    {
        new_payload[index] = (uint8_t)(index * 7U + 1U);
    }
    old_payload[0] = 0x00U;
    old_payload[1] = 0x00U;
    old_payload[2] = 0x02U;
    old_payload[3] = 0x20U;
    old_payload[4] = 0x01U;
    old_payload[5] = 0x01U;
    old_payload[6] = 0x01U;
    old_payload[7] = 0x08U;
    new_payload[0] = 0x00U;
    new_payload[1] = 0x00U;
    new_payload[2] = 0x02U;
    new_payload[3] = 0x20U;
    new_payload[4] = 0x01U;
    new_payload[5] = 0x01U;
    new_payload[6] = 0x01U;
    new_payload[7] = 0x08U;
    prepare_image(test.slot_a, old_payload, sizeof(old_payload), 1U);
    prepare_image(test.slot_b, new_payload, sizeof(new_payload), 2U);
    memcpy(test.app, old_payload, sizeof(old_payload));

    memset(&control, 0, sizeof(control));
    control.state = BOOT_CONTROL_UPDATE_READY;
    control.confirmed_slot = BOOT_SLOT_A;
    control.pending_slot = BOOT_SLOT_B;
    control.confirmed_version = 1U;
    control.pending_version = 2U;
    control.image_size = sizeof(new_payload);
    control.image_crc32 = boot_crc32_finish(
        boot_crc32_update(0xFFFFFFFFUL, new_payload, sizeof(new_payload)));
    append_bcb(&test, &control);

    ops.context = &test;
    assert(easy_bootloader_init(&config, &ops) == BOOT_LOADER_OK);
    easy_bootloader_get_progress(&progress);
    assert(test.jump_count == 1U);
    assert(test.app_erase_count == 1U);
    assert(memcmp(test.app, new_payload, sizeof(new_payload)) == 0);
    assert(progress.control.state == BOOT_CONTROL_TRIAL);
    assert(progress.control.boot_attempts == 1U);
    assert(progress.control.confirmed_slot == BOOT_SLOT_A);
    assert(progress.control.pending_slot == BOOT_SLOT_B);
}

static void test_installing_is_resumed_after_interruption(void)
{
    uint8_t old_payload[160];
    uint8_t new_payload[220];
    boot_control_status_t control;
    boot_loader_ops_t ops = test_ops_template;
    boot_loader_progress_t progress;
    boot_loader_config_t config = test_config();
    test_context_t test;
    uint32_t index;

    initialize_test(&test);
    for (index = 0U; index < sizeof(old_payload); index++)
    {
        old_payload[index] = (uint8_t)(index + 2U);
    }
    for (index = 0U; index < sizeof(new_payload); index++)
    {
        new_payload[index] = (uint8_t)(index * 5U + 9U);
    }
    old_payload[0] = 0x00U;
    old_payload[1] = 0x00U;
    old_payload[2] = 0x02U;
    old_payload[3] = 0x20U;
    old_payload[4] = 0x01U;
    old_payload[5] = 0x00U;
    old_payload[6] = 0x01U;
    old_payload[7] = 0x08U;
    new_payload[0] = 0x00U;
    new_payload[1] = 0x00U;
    new_payload[2] = 0x02U;
    new_payload[3] = 0x20U;
    new_payload[4] = 0x01U;
    new_payload[5] = 0x00U;
    new_payload[6] = 0x01U;
    new_payload[7] = 0x08U;
    prepare_image(test.slot_a, old_payload, sizeof(old_payload), 1U);
    prepare_image(test.slot_b, new_payload, sizeof(new_payload), 2U);

    memset(&control, 0, sizeof(control));
    control.state = BOOT_CONTROL_INSTALLING;
    control.confirmed_slot = BOOT_SLOT_A;
    control.pending_slot = BOOT_SLOT_B;
    control.pending_version = 2U;
    control.image_size = sizeof(new_payload);
    control.image_crc32 = boot_crc32_finish(
        boot_crc32_update(0xFFFFFFFFUL, new_payload, sizeof(new_payload)));
    append_bcb(&test, &control);

    ops.context = &test;
    assert(easy_bootloader_init(&config, &ops) == BOOT_LOADER_OK);
    easy_bootloader_get_progress(&progress);
    assert(test.jump_count == 1U);
    assert(test.app_erase_count == 1U);
    assert(memcmp(test.app, new_payload, sizeof(new_payload)) == 0);
    assert(progress.control.state == BOOT_CONTROL_TRIAL);
}

static void test_first_update_backs_up_running_app(void)
{
    uint8_t old_payload[280];
    uint8_t new_payload[220];
    boot_control_status_t control;
    boot_loader_ops_t ops = test_ops_template;
    boot_loader_progress_t progress;
    boot_loader_config_t config = test_config();
    boot_image_info_t backup;
    test_context_t test;
    uint32_t index;

    initialize_test(&test);
    for (index = 0U; index < sizeof(old_payload); index++)
    {
        old_payload[index] = (uint8_t)(index * 3U + 5U);
    }
    for (index = 0U; index < sizeof(new_payload); index++)
    {
        new_payload[index] = (uint8_t)(index * 11U + 7U);
    }
    old_payload[0] = 0x00U;
    old_payload[1] = 0x00U;
    old_payload[2] = 0x02U;
    old_payload[3] = 0x20U;
    old_payload[4] = 0x01U;
    old_payload[5] = 0x00U;
    old_payload[6] = 0x01U;
    old_payload[7] = 0x08U;
    new_payload[0] = 0x00U;
    new_payload[1] = 0x00U;
    new_payload[2] = 0x02U;
    new_payload[3] = 0x20U;
    new_payload[4] = 0x01U;
    new_payload[5] = 0x00U;
    new_payload[6] = 0x01U;
    new_payload[7] = 0x08U;
    memcpy(test.app, old_payload, sizeof(old_payload));
    prepare_image(test.slot_a, new_payload, sizeof(new_payload), 2U);

    memset(&control, 0, sizeof(control));
    control.state = BOOT_CONTROL_UPDATE_READY;
    control.pending_slot = BOOT_SLOT_A;
    control.pending_version = 2U;
    control.image_size = sizeof(new_payload);
    control.image_crc32 = boot_crc32_finish(
        boot_crc32_update(0xFFFFFFFFUL, new_payload, sizeof(new_payload)));
    append_bcb(&test, &control);

    ops.context = &test;
    assert(easy_bootloader_init(&config, &ops) == BOOT_LOADER_OK);
    easy_bootloader_get_progress(&progress);
    assert(test.jump_count == 1U);
    assert(test.app_erase_count == 1U);
    assert(memcmp(test.app, new_payload, sizeof(new_payload)) == 0);
    assert(progress.control.state == BOOT_CONTROL_TRIAL);
    assert(progress.control.confirmed_slot == BOOT_SLOT_B);
    assert(progress.control.pending_slot == BOOT_SLOT_A);
    assert(boot_image_header_decode(test.slot_b, &backup));
    assert(backup.payload_size == TEST_APP_SIZE);
    assert(memcmp(&test.slot_b[BOOT_IMAGE_PAYLOAD_OFFSET], old_payload,
                  sizeof(old_payload)) == 0);
}

static void test_trial_limit_rolls_back_to_confirmed_slot(void)
{
    uint8_t old_payload[160];
    uint8_t new_payload[220];
    boot_control_status_t control;
    boot_loader_ops_t ops = test_ops_template;
    boot_loader_progress_t progress;
    boot_loader_config_t config = test_config();
    test_context_t test;
    uint32_t index;

    initialize_test(&test);
    for (index = 0U; index < sizeof(old_payload); index++)
    {
        old_payload[index] = (uint8_t)(index + 15U);
    }
    for (index = 0U; index < sizeof(new_payload); index++)
    {
        new_payload[index] = (uint8_t)(index * 9U + 4U);
    }
    old_payload[0] = 0x00U;
    old_payload[1] = 0x00U;
    old_payload[2] = 0x02U;
    old_payload[3] = 0x20U;
    old_payload[4] = 0x01U;
    old_payload[5] = 0x00U;
    old_payload[6] = 0x01U;
    old_payload[7] = 0x08U;
    new_payload[0] = 0x00U;
    new_payload[1] = 0x00U;
    new_payload[2] = 0x02U;
    new_payload[3] = 0x20U;
    new_payload[4] = 0x01U;
    new_payload[5] = 0x00U;
    new_payload[6] = 0x01U;
    new_payload[7] = 0x08U;
    prepare_image(test.slot_a, old_payload, sizeof(old_payload), 1U);
    prepare_image(test.slot_b, new_payload, sizeof(new_payload), 2U);
    memcpy(test.app, new_payload, sizeof(new_payload));

    memset(&control, 0, sizeof(control));
    control.state = BOOT_CONTROL_TRIAL;
    control.confirmed_slot = BOOT_SLOT_A;
    control.pending_slot = BOOT_SLOT_B;
    control.pending_version = 2U;
    control.image_size = sizeof(new_payload);
    control.image_crc32 = boot_crc32_finish(
        boot_crc32_update(0xFFFFFFFFUL, new_payload, sizeof(new_payload)));
    control.boot_attempts = 2U;
    control.max_boot_attempts = 2U;
    append_bcb(&test, &control);

    ops.context = &test;
    assert(easy_bootloader_init(&config, &ops) == BOOT_LOADER_OK);
    easy_bootloader_get_progress(&progress);
    assert(test.jump_count == 1U);
    assert(memcmp(test.app, old_payload, sizeof(old_payload)) == 0);
    assert(progress.control.state == BOOT_CONTROL_CONFIRMED);
    assert(progress.control.confirmed_slot == BOOT_SLOT_A);
    assert(progress.control.pending_slot == BOOT_SLOT_NONE);
}

static void test_corrupt_pending_image_keeps_confirmed_app(void)
{
    uint8_t old_payload[160];
    boot_control_status_t control;
    boot_loader_ops_t ops = test_ops_template;
    boot_loader_progress_t progress;
    boot_loader_config_t config = test_config();
    test_context_t test;
    uint32_t index;

    initialize_test(&test);
    for (index = 0U; index < sizeof(old_payload); index++)
    {
        old_payload[index] = (uint8_t)(index + 21U);
    }
    old_payload[0] = 0x00U;
    old_payload[1] = 0x00U;
    old_payload[2] = 0x02U;
    old_payload[3] = 0x20U;
    old_payload[4] = 0x01U;
    old_payload[5] = 0x00U;
    old_payload[6] = 0x01U;
    old_payload[7] = 0x08U;
    prepare_image(test.slot_a, old_payload, sizeof(old_payload), 1U);
    memcpy(test.app, old_payload, sizeof(old_payload));

    memset(&control, 0, sizeof(control));
    control.state = BOOT_CONTROL_UPDATE_READY;
    control.confirmed_slot = BOOT_SLOT_A;
    control.pending_slot = BOOT_SLOT_B;
    control.pending_version = 2U;
    control.image_size = 128U;
    control.image_crc32 = 0x12345678UL;
    append_bcb(&test, &control);

    ops.context = &test;
    assert(easy_bootloader_init(&config, &ops) == BOOT_LOADER_OK);
    easy_bootloader_get_progress(&progress);
    assert(test.jump_count == 1U);
    assert(test.app_erase_count == 0U);
    assert(memcmp(test.app, old_payload, sizeof(old_payload)) == 0);
    assert(progress.control.state == BOOT_CONTROL_CONFIRMED);
}

static void test_bcb_ignores_incomplete_newest_record(void)
{
    boot_control_status_t confirmed;
    boot_control_status_t update;
    boot_control_status_t loaded;
    boot_control_storage_t storage;
    test_context_t test;

    initialize_test(&test);
    memset(&confirmed, 0, sizeof(confirmed));
    confirmed.state = BOOT_CONTROL_CONFIRMED;
    confirmed.confirmed_slot = BOOT_SLOT_A;
    append_bcb(&test, &confirmed);

    update = confirmed;
    update.state = BOOT_CONTROL_UPDATE_READY;
    update.pending_slot = BOOT_SLOT_B;
    /* The first 56 bytes imitate a power failure before commit marker. */
    memset(&test.bcb[BOOT_CONTROL_RECORD_SIZE], 0x00, 56U);

    storage.context = &test;
    storage.read = bcb_read_storage;
    storage.program = bcb_program_storage;
    storage.erase = bcb_erase_storage;
    storage.region_size = TEST_BCB_SIZE;
    assert(boot_control_load(&storage, &loaded));
    assert(loaded.state == BOOT_CONTROL_CONFIRMED);
    assert(loaded.confirmed_slot == BOOT_SLOT_A);
    (void)update;
}

static void test_first_backup_failure_keeps_old_app_bootable(void)
{
    uint8_t old_payload[160];
    uint8_t new_payload[220];
    boot_control_status_t control;
    boot_loader_ops_t ops = test_ops_template;
    boot_loader_progress_t progress;
    boot_loader_config_t config = test_config();
    test_context_t test;
    uint32_t index;

    initialize_test(&test);
    for (index = 0U; index < sizeof(old_payload); index++)
    {
        old_payload[index] = (uint8_t)(index + 31U);
    }
    for (index = 0U; index < sizeof(new_payload); index++)
    {
        new_payload[index] = (uint8_t)(index + 47U);
    }
    old_payload[0] = 0x00U;
    old_payload[1] = 0x00U;
    old_payload[2] = 0x02U;
    old_payload[3] = 0x20U;
    old_payload[4] = 0x01U;
    old_payload[5] = 0x00U;
    old_payload[6] = 0x01U;
    old_payload[7] = 0x08U;
    new_payload[0] = 0x00U;
    new_payload[1] = 0x00U;
    new_payload[2] = 0x02U;
    new_payload[3] = 0x20U;
    new_payload[4] = 0x01U;
    new_payload[5] = 0x00U;
    new_payload[6] = 0x01U;
    new_payload[7] = 0x08U;
    memcpy(test.app, old_payload, sizeof(old_payload));
    prepare_image(test.slot_a, new_payload, sizeof(new_payload), 2U);

    memset(&control, 0, sizeof(control));
    control.state = BOOT_CONTROL_UPDATE_READY;
    control.pending_slot = BOOT_SLOT_A;
    control.pending_version = 2U;
    control.image_size = sizeof(new_payload);
    control.image_crc32 = boot_crc32_finish(
        boot_crc32_update(0xFFFFFFFFUL, new_payload, sizeof(new_payload)));
    append_bcb(&test, &control);
    test.fail_external_erase = 1U;

    ops.context = &test;
    assert(easy_bootloader_init(&config, &ops) == BOOT_LOADER_OK);
    easy_bootloader_get_progress(&progress);
    assert(test.jump_count == 1U);
    assert(test.app_erase_count == 0U);
    assert(memcmp(test.app, old_payload, sizeof(old_payload)) == 0);
    assert(progress.control.state == BOOT_CONTROL_UPDATE_READY);
}

static void test_rebuilds_bcb_from_matching_external_image(void)
{
    uint8_t payload[220];
    boot_loader_ops_t ops = test_ops_template;
    boot_loader_progress_t progress;
    boot_loader_config_t config = test_config();
    test_context_t test;
    uint32_t index;

    initialize_test(&test);
    for (index = 0U; index < sizeof(payload); index++)
    {
        payload[index] = (uint8_t)(index * 13U + 17U);
    }
    payload[0] = 0x00U;
    payload[1] = 0x00U;
    payload[2] = 0x02U;
    payload[3] = 0x20U;
    payload[4] = 0x01U;
    payload[5] = 0x00U;
    payload[6] = 0x01U;
    payload[7] = 0x08U;
    memcpy(test.app, payload, sizeof(payload));
    prepare_image(test.slot_b, payload, sizeof(payload), 9U);

    ops.context = &test;
    assert(easy_bootloader_init(&config, &ops) == BOOT_LOADER_OK);
    easy_bootloader_get_progress(&progress);
    assert(test.jump_count == 1U);
    assert(progress.control.state == BOOT_CONTROL_CONFIRMED);
    assert(progress.control.confirmed_slot == BOOT_SLOT_B);
    assert(progress.control.confirmed_version == 9U);
}

int main(void)
{
    test_update_installs_and_enters_trial();
    test_installing_is_resumed_after_interruption();
    test_first_update_backs_up_running_app();
    test_trial_limit_rolls_back_to_confirmed_slot();
    test_corrupt_pending_image_keeps_confirmed_app();
    test_bcb_ignores_incomplete_newest_record();
    test_first_backup_failure_keeps_old_app_bootable();
    test_rebuilds_bcb_from_matching_external_image();
    puts("easy_bootloader tests passed");
    return 0;
}
