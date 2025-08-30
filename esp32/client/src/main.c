#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "setting.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "services/gap/ble_svc_gap.h"
#include "nimble/nimble_port_freertos.h"

#define TAG "CLIENT"
#define DEVICE_NAME "BLE_CLIENT"

#define GATT_SVC_UUID 0xABC0 /* 16 Bit Service UUID */
#define GATT_CHR_UUID 0xABC1 /* 16 Bit Service Characteristic UUID */

#define UART_NUM UART_NUM_0              // Using UART0
#define BUF_SIZE (3 * SOC_UART_FIFO_LEN) // Buffer size shall be greater than SOC_UART_FIFO_LEN
#define MSGLEN 3                         // Message length
#define SERVER_BAUDRATE 1048576

static int client_gap_event(struct ble_gap_event *event, void *arg);

static uint8_t own_addr_type;
static uint16_t connection;
static ble_addr_t peer_addr;
static uint16_t chrval_handle;

// For random static address, 2 MSB bits of the first byte shall be 0b11.
// I.e. addr[5] shall be in the range of 0xC0 to 0xFF
static const uint8_t server_addr[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0xC0};
static const uint8_t client_addr[] = {0x10, 0x20, 0x30, 0x40, 0x50, 0xC0};

// Initiates the GAP general discovery procedure.
static void client_scan(void)
{
    struct ble_gap_disc_params disc_params = {0};

    disc_params.passive = 1;           /* Perform a passive scan. */
    disc_params.filter_duplicates = 1; /* Avoid processing repeated advertisements from the same device. */

    int status = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params, client_gap_event, NULL);

    if (status != 0)
    {
        ESP_LOGE(TAG, "Error initiating GAP discovery procedure; rc=%d\n", status);
    }
}

static void client_connect(const struct ble_gap_disc_desc *disc)
{
    /* Scanning must be stopped before a connection can be initiated. */
    int status = ble_gap_disc_cancel();

    if (status == 0)
    {
        /* Try to connect the advertiser. 30 seconds timeout. It can be BLE_HS_FOREVER */
        status = ble_gap_connect(own_addr_type, &disc->addr, 30000, NULL, client_gap_event, NULL);

        if (status != 0)
        {
            char addr_str[18] = {0};
            sprintf(addr_str, "%02X:%02X:%02X:%02X:%02X:%02X",
                    disc->addr.val[5], disc->addr.val[4], disc->addr.val[3],
                    disc->addr.val[2], disc->addr.val[1], disc->addr.val[0]);
            ESP_LOGE(TAG, "Error: Failed to connect to device; addr_type=%d addr=%s; status=%d\n", disc->addr.type, addr_str, status);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Failed to cancel scan; status=%d\n", status);
    }
}

static int on_descriptor_discovery(uint16_t conn_handle, const struct ble_gatt_error *error, uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *)
{
    if (error->status == 0 && (dsc != NULL))
    {
        if (0 == ble_uuid_cmp(&dsc->uuid.u, BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16)))
        {
            /* Subscribe to notifications for the characteristic.
             * A central enables notifications by writing two bytes (0x01 00) to the
             * characteristic's client-characteristic-configuration-descriptor (CCCD).
             * Notification: 0x01 00, Indication: 0x02 00 and Disable both: 0x00 00
             */
            uint8_t value[2] = {1, 0};
            assert(0 == ble_gattc_write_flat(conn_handle, dsc->handle, value, sizeof(value), NULL, NULL));
        }
    }
    else if (error->status == BLE_HS_EDONE)
    {
        ESP_LOGI(TAG, "Descriptor discovery complete.");
    }
    else
    {
        ESP_LOGE(TAG, "Descriptor discovery failed: %d", error->status);
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }

    return 0;
}

static int on_characteristic_discovery(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_chr *chr, void *arg)
{
    if ((error->status == 0) && (chr != NULL))
    {
        chrval_handle = chr->val_handle;

        ESP_LOGI(TAG, "Discovered characteristics, discovering descriptors...");
        assert(0 == ble_gattc_disc_all_dscs(conn_handle, chr->val_handle, chr->val_handle + 1, on_descriptor_discovery, NULL));
    }
    else if (error->status == BLE_HS_EDONE)
    {
        ESP_LOGI(TAG, "Characteristic discovery complete.");
    }
    else
    {
        ESP_LOGE(TAG, "Characteristic discovery error: %d", error->status);
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }

    return 0;
}

static int on_service_discovery(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_svc *service, void *arg)
{
    if ((error->status == 0) && (service != NULL))
    {
        uint16_t svc_start_handle = service->start_handle;
        uint16_t svc_end_handle = service->end_handle;

        ESP_LOGI(TAG, "Discovered service, discovering characteristics...");
        assert(0 == ble_gattc_disc_chrs_by_uuid(conn_handle, svc_start_handle, svc_end_handle,
                                                BLE_UUID16_DECLARE(GATT_CHR_UUID), on_characteristic_discovery, NULL));
    }
    else if (error->status == BLE_HS_EDONE)
    {
        ESP_LOGI(TAG, "Service discovery complete.");
    }
    else
    {
        ESP_LOGE(TAG, "Service discovery failed; status=%d\n", error->status);
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }

    return 0;
}

static int client_gap_event(struct ble_gap_event *event, void *)
{
    int status = 0;
    struct ble_gap_conn_desc desc;
    struct ble_hs_adv_fields fields;

    switch (event->type)
    {
    case BLE_GAP_EVENT_DISC:
        status = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
        if (status == 0)
        {
            bool connected = false;
            /* Check if device is already connected or not */
            if (0 == memcmp(peer_addr.val, event->disc.addr.val, sizeof(event->disc.addr.val)))
            {
                ESP_LOGI(TAG, "Device already connected");
                connected = true;
                break;
            }

            if (!connected)
            {
                /* The device has to be advertising connectability. */
                if ((event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND) || (event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND))
                {
                    if (0 == memcmp(event->disc.addr.val, server_addr, sizeof(server_addr)))
                    {
                        /* The device has to advertise support for the service. */
                        for (int i = 0; i < fields.num_uuids16; i++)
                        {
                            if (ble_uuid_u16(&fields.uuids16[i].u) == GATT_SVC_UUID)
                            {
                                /* Try to connect to the advertiser. */
                                client_connect(&event->disc);
                                break;
                            }
                        }
                    }
                }
            }
        }
        break;

    case BLE_GAP_EVENT_CONNECT: /* A new connection was established or a connection attempt failed. */
        if (event->connect.status == 0)
        {
            /* Connection successfully established. */
            ESP_LOGI(TAG, "Connection established ");
            assert(0 == ble_gap_conn_find(event->connect.conn_handle, &desc));

            connection = event->connect.conn_handle;
            memcpy(peer_addr.val, desc.peer_id_addr.val, sizeof(desc.peer_id_addr.val));

            assert(0 == ble_gattc_disc_svc_by_uuid(event->connect.conn_handle, BLE_UUID16_DECLARE(GATT_SVC_UUID), on_service_discovery, NULL));
        }
        else
        {
            /* Connection attempt failed; resume scanning. */
            ESP_LOGE(TAG, "Error: Connection failed; status=%d\n", event->connect.status);
            client_scan();
        }

        break;

    case BLE_GAP_EVENT_DISCONNECT:
        /* Connection terminated. */
        ESP_LOGI(TAG, "disconnect; reason=%d ", event->disconnect.reason);

        /* Forget about peer. */
        memset(peer_addr.val, 0, sizeof(peer_addr.val));
        chrval_handle = 0;

        /* Resume scanning. */
        client_scan();
        break;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(TAG, "discovery complete; reason=%d\n", event->disc_complete.reason);
        break;

    case BLE_GAP_EVENT_NOTIFY_RX:
    {
        /* Peer sent us a notification or indication. */
        ESP_LOGI(TAG, "received %s; conn_handle=%d attr_handle=%d  attr_len=%d\n",
                 event->notify_rx.indication ? "indication" : "notification",
                 event->notify_rx.conn_handle, event->notify_rx.attr_handle,
                 OS_MBUF_PKTLEN(event->notify_rx.om));

        char buffer[OS_MBUF_PKTLEN(event->notify_rx.om)];
        memset(buffer, 0, sizeof(buffer));

        // Attribute data is in event->notify_rx.om.
        assert(0 == os_mbuf_copydata(event->notify_rx.om, 0, sizeof(buffer), buffer));

        if (MSGLEN != uart_write_bytes(UART_NUM, buffer, MSGLEN))
        {
            ESP_LOGE(TAG, "Failed to write");
        }
    }
    break;

    case BLE_GAP_EVENT_MTU:
        /* Maximum Transmission Unit defines the maximum size of a single ATT (Attribute Protocol) payload,
         i.e., how much data can be sent in a single BLE GATT read/write/notify/indication operation. */
        ESP_LOGI(TAG, "MTU update event; conn_handle=%d cid=%d mtu=%d\n",
                 event->mtu.conn_handle, event->mtu.channel_id, event->mtu.value);
        break;

    default:
        break;
    }

    return status;
}

static void client_on_reset(int reason)
{
    ESP_LOGE(TAG, "Resetting state; reason=%d\n", reason);
}

static void client_on_sync(void)
{
    assert(0 == ble_hs_id_set_rnd(client_addr)); // Set random static address; BLE_ADDR_RANDOM

    /* Make sure we have proper identity address set (public preferred) */
    assert(0 == ble_hs_util_ensure_addr(0));

    /* Figure out address to use while advertising */
    assert(0 == ble_hs_id_infer_auto(0, &own_addr_type));

    uint8_t addr[sizeof(server_addr)] = {0};
    assert(0 == ble_hs_id_copy_addr(own_addr_type, addr, NULL));

    printf("BLE Device Address: %02X:%02X:%02X:%02X:%02X:%02X\n", addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);

    /* Begin scanning for a peripheral to connect to. */
    client_scan();
}

void app_main(void)
{
    esp_err_t status = nvs_flash_init();
    if (status == ESP_ERR_NVS_NO_FREE_PAGES || status == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        status = nvs_flash_init();
    }
    ESP_ERROR_CHECK(status);

    ESP_ERROR_CHECK(nimble_port_init());

    /* Configure the host. */
    ble_hs_cfg.reset_cb = client_on_reset;
    ble_hs_cfg.sync_cb = client_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* Set the default device name. */
    assert(0 == ble_svc_gap_device_name_set(DEVICE_NAME));

    uart_config_t config = {
        .baud_rate = SERVER_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // Install driver and configure UART
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, BUF_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "UART initialized");

    ESP_LOGI(TAG, "BLE Host Task Started");
    nimble_port_run(); /* This function will return only when nimble_port_stop() is executed */
    nimble_port_freertos_deinit();
}