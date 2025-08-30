/**
 * What this code does:
 *
 *          1. Initializes BLE and UART (at 115200 baud).
 *           2. Scans for and connects to a specific BLE server.
 *           3. Upon connection, discovers services and characteristics.
 *           4. Reads data from the UART buffer and sends it to the connected BLE server.
 */

// This code is Nimble, part of Bluetooth Low Energy, role: is  => Central(Client) take in data, send data to Client.
// Which means it connects to a BLE peripheral (Server/Slave) and reads/writes data from and to it.
// It gets the data from the peripheral.

// TL;DR: BLE client scans contuniously for BLE advertisment messages.(in simple terms, think of it like youtube ads, looking for customers, customers looking for products).

#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "services/gap/ble_svc_gap.h"
#include "nimble/nimble_port_freertos.h"
#include "driver/uart.h"       // Required for UART functionality
#include "freertos/FreeRTOS.h" // Required for FreeRTOS types
#include "freertos/task.h"     // Required for vTaskDelay

#define ON '1'
#define OFF '0'
#define TAG "CLIENT"
#define DEVICE_NAME "BLE_CLIENT"

#define GATT_SVC_UUID 0xABC0 /* 16 Bit Service UUID */
#define GATT_CHR_UUID 0xABC1 /* 16 Bit Service Characteristic UUID */

// UART Defines
#define UART_NUM UART_NUM_0
#define UART_BUF_SIZE 1024

static int client_gap_event(struct ble_gap_event *event, void *arg);

// This MUST be the same as 'server_addr' in the server code.
static const uint8_t server_addr[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0xD0};

// --- This is the client's own hardcoded address ---
// Note: Last byte (0xC0) starts with '11' in binary (11000000), valid static address.
static const uint8_t client_addr[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xC0};

static bool ARE_WE_CONNECTED = false;
static uint8_t own_addr_type;
static uint16_t connection;
static ble_addr_t peer_addr;
static uint16_t chrval_handle; // Characteristic value handle


/**
 * @brief Initializes the UART driver.
 */
static void uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    // Install UART driver
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
    // Set UART pins (using UART0 default pins)
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "UART driver initialized");
}

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

static int on_descriptor_discovery(uint16_t conn_handle, const struct ble_gatt_error *error, uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg)
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

static int client_gap_event(struct ble_gap_event *event, void *arg)
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
            ARE_WE_CONNECTED = true; // Set connection flag
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
        ARE_WE_CONNECTED = false; // Clear connection flag

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

        // Attribute data is contained in event->notify_rx.om.
        assert(0 == os_mbuf_copydata(event->notify_rx.om, 0, sizeof(buffer), buffer));

        printf(" => %s\n", (buffer[0] == 1) ? "done" : "failed");
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

// --- MODIFY THIS FUNCTION ---
static void client_on_sync(void)
{
    int rc;
    printf("***Client has synced with the server!***\n");

    // Set the client's own hardcoded random static address
    rc = ble_hs_id_set_rnd(client_addr);
    assert(rc == 0);

    // We must now use a random address type
    own_addr_type = BLE_ADDR_RANDOM;

    // Log the client's address
    char addr_str[18];
    sprintf(addr_str, "%02X:%02X:%02X:%02X:%02X:%02X",
            client_addr[5], client_addr[4], client_addr[3],
            client_addr[2], client_addr[1], client_addr[0]);
    printf("Client static address set to: %s\n", addr_str);

    /* Begin scanning for the hardcoded server address. */
    client_scan();
}
/**
 * @brief FreeRTOS task to read data from UART and send it over BLE.
 */
void client_task(void *pvParameters)
{
    (void)pvParameters;
    static uint8_t uart_buffer[UART_BUF_SIZE];

    while (1)
    {
        // Only proceed if connected and the characteristic handle is valid
        if (ARE_WE_CONNECTED && chrval_handle != 0)
        {
            // Read data from UART. The 20ms timeout makes this non-blocking if no data is available.
            int len = uart_read_bytes(UART_NUM, uart_buffer, sizeof(uart_buffer) - 1, pdMS_TO_TICKS(20));

            if (len > 0)
            {
                ESP_LOGI(TAG, "Read %d bytes from UART. Sending over BLE.", len);

                // Write the data from the UART buffer to the BLE characteristic
                int status = ble_gattc_write_flat(connection, chrval_handle, uart_buffer, len, NULL, NULL);
                if (status != 0)
                {
                    ESP_LOGE(TAG, "Error: Failed to write characteristic; status=%d", status);
                }
            }
        }
        else
        {
            // If not connected, wait before checking again to avoid busy-looping
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
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

    // Initialize UART
    uart_init();

    ESP_ERROR_CHECK(nimble_port_init());

    /* Configure the host. */
    ble_hs_cfg.reset_cb = client_on_reset;
    ble_hs_cfg.sync_cb = client_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* Set the default device name. */
    assert(0 == ble_svc_gap_device_name_set(DEVICE_NAME));

    assert(pdTRUE == xTaskCreate(client_task, "client_task", 4096, NULL, 8, NULL));

    ESP_LOGI(TAG, "BLE Host Task Started");
    nimble_port_run(); /* This function will return only when nimble_port_stop() is executed */
    nimble_port_freertos_deinit();
}