#include "easy_bootloader_app.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define TEST_STORAGE_SIZE  (16U * 1024U)
#define TEST_RX_SIZE       (8U * 1024U)
#define TEST_TX_SIZE       128U

typedef struct
{
    uint8_t storage_a[TEST_STORAGE_SIZE];
    uint8_t storage_b[TEST_STORAGE_SIZE];
    uint8_t rx[TEST_RX_SIZE];
    uint32_t rx_read;
    uint32_t rx_write;
    uint8_t tx[TEST_TX_SIZE];
    uint32_t tx_length;
    uint32_t now_ms;
    uint32_t ack_count;
    uint32_t reset_count;
    uint32_t mark_count;
    uint32_t confirm_count;
    uint8_t bcb[BOOT_CONTROL_RECORD_SIZE * 16U];
    boot_image_info_t marked_image;
    boot_control_status_t control;
    boot_slot_t marked_slot;
} test_context_t;

static uint32_t test_time_ms(void *context)
{
    return ((test_context_t *)context)->now_ms;
}

static uint32_t test_transport_read(void *context, uint8_t *data, uint32_t capacity)
{
    test_context_t *test = (test_context_t *)context;
    uint32_t available = test->rx_write - test->rx_read;
    uint32_t length = (available < capacity) ? available : capacity;

    memcpy(data, &test->rx[test->rx_read], length);
    test->rx_read += length;
    return length;
}

static boot_app_status_t test_transport_write(void *context,
                                               const uint8_t *data,
                                               uint32_t length)
{
    static const uint8_t ack[] = {0x55U, 0xAAU, 0xFFU, 0xFEU, 0x55U, 0x55U};
    test_context_t *test = (test_context_t *)context;

    if ((length == sizeof(ack)) && (memcmp(data, ack, sizeof(ack)) == 0))
    {
        test->ack_count++;
    }
    else
    {
        assert(length <= (TEST_TX_SIZE - test->tx_length));
        memcpy(&test->tx[test->tx_length], data, length);
        test->tx_length += length;
    }
    return BOOT_APP_OK;
}

static boot_app_status_t test_read_boot_control(void *context,
                                                boot_control_status_t *status)
{
    *status = ((test_context_t *)context)->control;
    return BOOT_APP_OK;
}

static boot_app_status_t test_bcb_read(void *context,
                                       uint32_t offset,
                                       uint8_t *data,
                                       uint32_t length)
{
    test_context_t *test = (test_context_t *)context;
    if ((offset > sizeof(test->bcb)) || (length > (sizeof(test->bcb) - offset)))
    {
        return BOOT_APP_IO_ERROR;
    }
    memcpy(data, &test->bcb[offset], length);
    return BOOT_APP_OK;
}

static boot_app_status_t test_bcb_program(void *context,
                                          uint32_t offset,
                                          const uint8_t *data,
                                          uint32_t length)
{
    test_context_t *test = (test_context_t *)context;
    uint32_t index;

    if ((offset > sizeof(test->bcb)) || (length > (sizeof(test->bcb) - offset)))
    {
        return BOOT_APP_IO_ERROR;
    }
    for (index = 0U; index < length; index++)
    {
        if ((test->bcb[offset + index] & data[index]) != data[index])
        {
            return BOOT_APP_IO_ERROR;
        }
        test->bcb[offset + index] &= data[index];
    }
    return BOOT_APP_OK;
}

static boot_app_status_t test_bcb_erase(void *context,
                                        uint32_t offset,
                                        uint32_t length)
{
    test_context_t *test = (test_context_t *)context;
    if ((offset > sizeof(test->bcb)) || (length > (sizeof(test->bcb) - offset)))
    {
        return BOOT_APP_IO_ERROR;
    }
    memset(&test->bcb[offset], 0xFF, length);
    return BOOT_APP_OK;
}

static uint8_t *test_slot_storage(test_context_t *test, boot_slot_t slot)
{
    if (slot == BOOT_SLOT_A)
    {
        return test->storage_a;
    }
    if (slot == BOOT_SLOT_B)
    {
        return test->storage_b;
    }
    return NULL;
}

static boot_app_status_t test_storage_erase(void *context,
                                            boot_slot_t slot,
                                            uint32_t offset,
                                            uint32_t length)
{
    test_context_t *test = (test_context_t *)context;
    uint8_t *storage = test_slot_storage(test, slot);
    if ((storage == NULL) || (offset > TEST_STORAGE_SIZE) ||
        (length > (TEST_STORAGE_SIZE - offset)))
    {
        return BOOT_APP_IO_ERROR;
    }
    memset(&storage[offset], 0xFF, length);
    return BOOT_APP_OK;
}

static boot_app_status_t test_storage_write(void *context,
                                            boot_slot_t slot,
                                            uint32_t offset,
                                            const uint8_t *data,
                                            uint32_t length)
{
    test_context_t *test = (test_context_t *)context;
    uint8_t *storage = test_slot_storage(test, slot);
    uint32_t index;

    if ((storage == NULL) || (offset > TEST_STORAGE_SIZE) ||
        (length > (TEST_STORAGE_SIZE - offset)))
    {
        return BOOT_APP_IO_ERROR;
    }
    for (index = 0U; index < length; index++)
    {
        if ((storage[offset + index] & data[index]) != data[index])
        {
            return BOOT_APP_IO_ERROR;
        }
        storage[offset + index] &= data[index];
    }
    return BOOT_APP_OK;
}

static boot_app_status_t test_storage_read(void *context,
                                           boot_slot_t slot,
                                           uint32_t offset,
                                           uint8_t *data,
                                           uint32_t length)
{
    test_context_t *test = (test_context_t *)context;
    uint8_t *storage = test_slot_storage(test, slot);
    if ((storage == NULL) || (offset > TEST_STORAGE_SIZE) ||
        (length > (TEST_STORAGE_SIZE - offset)))
    {
        return BOOT_APP_IO_ERROR;
    }
    memcpy(data, &storage[offset], length);
    return BOOT_APP_OK;
}

static boot_app_status_t test_mark_ready(void *context,
                                         boot_slot_t pending_slot,
                                         const boot_image_info_t *image)
{
    test_context_t *test = (test_context_t *)context;
    test->marked_image = *image;
    test->marked_slot = pending_slot;
    test->mark_count++;
    return BOOT_APP_OK;
}

static boot_app_status_t test_mark_confirmed(void *context,
                                             const boot_control_status_t *status)
{
    test_context_t *test = (test_context_t *)context;
    test->control = *status;
    test->confirm_count++;
    return BOOT_APP_OK;
}

static void test_reset(void *context)
{
    ((test_context_t *)context)->reset_count++;
}

static const boot_app_ops_t test_ops_template =
{
    NULL,
    test_time_ms,
    test_transport_read,
    test_transport_write,
    test_read_boot_control,
    test_bcb_read,
    test_bcb_program,
    test_bcb_erase,
    test_storage_erase,
    test_storage_write,
    test_storage_read,
    test_mark_ready,
    test_mark_confirmed,
    test_reset,
    NULL,
};

static void queue_bytes(test_context_t *test, const uint8_t *data, uint32_t length)
{
    assert(length <= (TEST_RX_SIZE - test->rx_write));
    memcpy(&test->rx[test->rx_write], data, length);
    test->rx_write += length;
}

static uint32_t make_data_frame(uint8_t *frame,
                                const uint8_t *payload,
                                uint16_t payload_length,
                                uint32_t remaining)
{
    uint16_t checksum = 0U;
    uint32_t index;
    uint32_t checksum_offset;

    frame[0] = 0x55U;
    frame[1] = 0xAAU;
    frame[2] = (uint8_t)(remaining >> 16);
    frame[3] = (uint8_t)(remaining >> 8);
    frame[4] = (uint8_t)remaining;
    frame[5] = (uint8_t)(payload_length >> 8);
    frame[6] = (uint8_t)payload_length;
    memcpy(&frame[7], payload, payload_length);

    checksum_offset = 7U + payload_length;
    for (index = 5U; index < checksum_offset; index++)
    {
        checksum = (uint16_t)(checksum + frame[index]);
    }
    frame[checksum_offset] = (uint8_t)(checksum >> 8);
    frame[checksum_offset + 1U] = (uint8_t)checksum;
    frame[checksum_offset + 2U] = 0x55U;
    frame[checksum_offset + 3U] = 0x55U;
    return payload_length + 11U;
}

static void queue_finish(test_context_t *test, uint32_t version, uint32_t date)
{
    uint8_t frame[14] =
    {
        0x55U, 0xAAU,
        (uint8_t)(version >> 24), (uint8_t)(version >> 16),
        (uint8_t)(version >> 8), (uint8_t)version,
        (uint8_t)(date >> 24), (uint8_t)(date >> 16),
        (uint8_t)(date >> 8), (uint8_t)date,
        0xFFU, 0xFDU, 0x55U, 0x55U,
    };
    queue_bytes(test, frame, sizeof(frame));
}

static void run_until_rx_empty(test_context_t *test)
{
    uint32_t guard = 32U;
    do
    {
        easy_bootloader_app_run();
        assert(guard-- > 0U);
    } while (test->rx_read != test->rx_write);
}

static void test_successful_staging(void)
{
    static const uint8_t start[] = {0x55U, 0xAAU, 0xFFU, 0xEEU, 0x55U, 0x55U};
    uint8_t firmware[1500];
    uint8_t frame[1024];
    uint32_t frame_length;
    uint32_t index;
    boot_app_config_t config;
    boot_app_ops_t ops = test_ops_template;
    boot_app_progress_t progress;
    boot_image_info_t header;
    test_context_t test;

    memset(&test, 0, sizeof(test));
    memset(test.storage_a, 0xA5, sizeof(test.storage_a));
    memset(test.storage_b, 0xA5, sizeof(test.storage_b));
    memset(test.bcb, 0xFF, sizeof(test.bcb));
    test.control.state = BOOT_CONTROL_CONFIRMED;
    test.control.confirmed_slot = BOOT_SLOT_A;
    ops.context = &test;
    easy_bootloader_app_get_default_config(&config);
    config.slot_a_size = TEST_STORAGE_SIZE;
    config.slot_b_size = TEST_STORAGE_SIZE;
    config.image_max_size = 8U * 1024U;
    config.bcb_region_size = sizeof(test.bcb);
    config.auto_reset = 1U;
    assert(easy_bootloader_app_init(&config, &ops) == BOOT_APP_OK);

    for (index = 0U; index < sizeof(firmware); index++)
    {
        firmware[index] = (uint8_t)(index * 37U + 11U);
    }

    queue_bytes(&test, start, sizeof(start));
    run_until_rx_empty(&test);
    assert(test.ack_count == 1U);

    frame_length = make_data_frame(frame, firmware, 600U, 900U);
    queue_bytes(&test, frame, frame_length);
    run_until_rx_empty(&test);

    frame_length = make_data_frame(frame, &firmware[600], 900U, 0U);
    queue_bytes(&test, frame, frame_length);
    run_until_rx_empty(&test);

    queue_finish(&test, 7U, 0x20260812UL);
    run_until_rx_empty(&test);

    easy_bootloader_app_get_progress(&progress);
    assert(progress.state == BOOT_APP_STATE_READY);
    assert(progress.received_size == sizeof(firmware));
    assert(progress.target_slot == BOOT_SLOT_B);
    assert(test.ack_count == 4U);
    assert(test.mark_count == 1U);
    assert(test.reset_count == 1U);
    assert(test.marked_slot == BOOT_SLOT_B);
    assert(memcmp(&test.storage_b[BOOT_IMAGE_PAYLOAD_OFFSET],
                  firmware,
                  sizeof(firmware)) == 0);
    assert(boot_image_header_decode(test.storage_b, &header));
    assert(test.storage_a[0] == 0xA5U);
    assert(header.target_address == BOOT_APP_DEFAULT_TARGET_ADDRESS);
    assert(header.firmware_version == 7U);
    assert(header.build_date == 0x20260812UL);
    assert(header.payload_size == sizeof(firmware));
    assert(header.payload_crc32 == test.marked_image.payload_crc32);
}

static void test_version_and_bcd_date_queries(void)
{
    static const uint8_t query_version[] =
        {0x55U, 0xAAU, 0xFFU, 0xDDU, 0x55U, 0x55U};
    static const uint8_t query_date[] =
        {0x55U, 0xAAU, 0xFFU, 0xCCU, 0x55U, 0x55U};
    static const uint8_t expected[] =
        "version:7\r\n2026-08-13\r\n";
    boot_app_config_t config;
    boot_app_ops_t ops = test_ops_template;
    test_context_t test;

    memset(&test, 0, sizeof(test));
    memset(test.storage_a, 0xFF, sizeof(test.storage_a));
    memset(test.storage_b, 0xFF, sizeof(test.storage_b));
    memset(test.bcb, 0xFF, sizeof(test.bcb));
    ops.context = &test;
    easy_bootloader_app_get_default_config(&config);
    config.slot_a_size = TEST_STORAGE_SIZE;
    config.slot_b_size = TEST_STORAGE_SIZE;
    config.image_max_size = 8U * 1024U;
    config.bcb_region_size = sizeof(test.bcb);
    config.running_version = 7U;
    config.running_build_date = 0x20260813UL;
    config.auto_reset = 0U;
    assert(easy_bootloader_app_init(&config, &ops) == BOOT_APP_OK);

    queue_bytes(&test, query_version, sizeof(query_version));
    queue_bytes(&test, query_date, sizeof(query_date));
    run_until_rx_empty(&test);

    assert(test.tx_length == (sizeof(expected) - 1U));
    assert(memcmp(test.tx, expected, sizeof(expected) - 1U) == 0);
}

static void test_remaining_mismatch_is_rejected(void)
{
    static const uint8_t start[] = {0x55U, 0xAAU, 0xFFU, 0xEEU, 0x55U, 0x55U};
    uint8_t payload[16] = {0};
    uint8_t frame[64];
    uint32_t frame_length;
    boot_app_config_t config;
    boot_app_ops_t ops = test_ops_template;
    boot_app_progress_t progress;
    test_context_t test;

    memset(&test, 0, sizeof(test));
    memset(test.storage_a, 0xFF, sizeof(test.storage_a));
    memset(test.storage_b, 0xFF, sizeof(test.storage_b));
    memset(test.bcb, 0xFF, sizeof(test.bcb));
    test.control.state = BOOT_CONTROL_CONFIRMED;
    test.control.confirmed_slot = BOOT_SLOT_B;
    ops.context = &test;
    easy_bootloader_app_get_default_config(&config);
    config.slot_a_size = TEST_STORAGE_SIZE;
    config.slot_b_size = TEST_STORAGE_SIZE;
    config.image_max_size = 8U * 1024U;
    config.bcb_region_size = sizeof(test.bcb);
    config.auto_reset = 0U;
    assert(easy_bootloader_app_init(&config, &ops) == BOOT_APP_OK);

    queue_bytes(&test, start, sizeof(start));
    run_until_rx_empty(&test);
    frame_length = make_data_frame(frame, payload, sizeof(payload), 16U);
    queue_bytes(&test, frame, frame_length);
    run_until_rx_empty(&test);

    frame_length = make_data_frame(frame, payload, sizeof(payload), 1U);
    queue_bytes(&test, frame, frame_length);
    run_until_rx_empty(&test);

    easy_bootloader_app_get_progress(&progress);
    assert(progress.state == BOOT_APP_STATE_ERROR);
    assert(progress.last_error == BOOT_APP_PROTOCOL_ERROR);
    assert(test.mark_count == 0U);
    assert(progress.target_slot == BOOT_SLOT_A);
}

static void test_unconfirmed_state_blocks_download(void)
{
    static const uint8_t start[] = {0x55U, 0xAAU, 0xFFU, 0xEEU, 0x55U, 0x55U};
    boot_app_config_t config;
    boot_app_ops_t ops = test_ops_template;
    boot_app_progress_t progress;
    test_context_t test;

    memset(&test, 0, sizeof(test));
    memset(test.storage_a, 0xFF, sizeof(test.storage_a));
    memset(test.storage_b, 0xFF, sizeof(test.storage_b));
    memset(test.bcb, 0xFF, sizeof(test.bcb));
    test.control.state = BOOT_CONTROL_TRIAL;
    test.control.confirmed_slot = BOOT_SLOT_A;
    test.control.pending_slot = BOOT_SLOT_B;
    ops.context = &test;
    easy_bootloader_app_get_default_config(&config);
    config.slot_a_size = TEST_STORAGE_SIZE;
    config.slot_b_size = TEST_STORAGE_SIZE;
    config.image_max_size = 8U * 1024U;
    config.bcb_region_size = sizeof(test.bcb);
    config.auto_reset = 0U;
    assert(easy_bootloader_app_init(&config, &ops) == BOOT_APP_OK);

    queue_bytes(&test, start, sizeof(start));
    run_until_rx_empty(&test);

    easy_bootloader_app_get_progress(&progress);
    assert(progress.state == BOOT_APP_STATE_ERROR);
    assert(progress.last_error == BOOT_APP_BUSY);
    assert(progress.target_slot == BOOT_SLOT_NONE);
    assert(test.ack_count == 0U);
}

static void test_image_header_requires_commit_and_crc(void)
{
    uint8_t raw[BOOT_IMAGE_HEADER_SIZE];
    boot_image_info_t source;
    boot_image_info_t decoded;

    memset(&source, 0, sizeof(source));
    source.target_address = BOOT_APP_DEFAULT_TARGET_ADDRESS;
    source.firmware_version = 3U;
    source.build_date = 0x20260812UL;
    source.payload_offset = BOOT_IMAGE_PAYLOAD_OFFSET;
    source.payload_size = 4096U;
    source.payload_crc32 = 0x12345678UL;
    boot_image_header_encode(&source, raw);
    assert(boot_image_header_decode(raw, &decoded));

    raw[60] = 0xFFU;
    assert(!boot_image_header_decode(raw, &decoded));
    boot_image_header_encode(&source, raw);
    raw[12] ^= 0x01U;
    assert(!boot_image_header_decode(raw, &decoded));
}

static void test_trial_image_can_be_confirmed(void)
{
    boot_app_config_t config;
    boot_app_ops_t ops = test_ops_template;
    test_context_t test;

    memset(&test, 0, sizeof(test));
    memset(test.storage_a, 0xFF, sizeof(test.storage_a));
    memset(test.storage_b, 0xFF, sizeof(test.storage_b));
    memset(test.bcb, 0xFF, sizeof(test.bcb));
    test.control.state = BOOT_CONTROL_TRIAL;
    test.control.confirmed_slot = BOOT_SLOT_A;
    test.control.pending_slot = BOOT_SLOT_B;
    test.control.pending_version = 8U;
    test.control.image_size = 1500U;
    test.control.image_crc32 = 0xB6CD5F12UL;
    ops.context = &test;
    easy_bootloader_app_get_default_config(&config);
    config.slot_a_size = TEST_STORAGE_SIZE;
    config.slot_b_size = TEST_STORAGE_SIZE;
    config.image_max_size = 8U * 1024U;
    config.bcb_region_size = sizeof(test.bcb);
    config.auto_reset = 0U;
    assert(easy_bootloader_app_init(&config, &ops) == BOOT_APP_OK);
    assert(easy_bootloader_app_confirm_running() == BOOT_APP_OK);
    assert(test.confirm_count == 1U);
    assert(test.control.state == BOOT_CONTROL_CONFIRMED);
    assert(test.control.confirmed_slot == BOOT_SLOT_B);
    assert(test.control.confirmed_version == 8U);
    assert(test.control.pending_slot == BOOT_SLOT_NONE);
}

int main(void)
{
    test_successful_staging();
    test_version_and_bcd_date_queries();
    test_remaining_mismatch_is_rejected();
    test_unconfirmed_state_blocks_download();
    test_image_header_requires_commit_and_crc();
    test_trial_image_can_be_confirmed();
    puts("easy_bootloader_app tests passed");
    return 0;
}
