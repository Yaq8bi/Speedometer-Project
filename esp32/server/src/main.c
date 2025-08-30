#include "esp_bt.h"
#include "setting.h"
#include "esp_log.h"
#include <stdbool.h>
#include "nvs_flash.h"
#include "nimble/ble.h"
#include "host/ble_hs.h"
#include "driver/uart.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "nimble/nimble_port_freertos.h"

#define TAG "SERVER"
#define DEVICE_NAME "BLE_SERVER"
#define UART_NUM UART_NUM_0              // Using UART0
#define BUF_SIZE (3 * SOC_UART_FIFO_LEN) // Buffer size shall be greater than SOC_UART_FIFO_LEN
#define MSGLEN 3                         // Message length
#define SERVER_BAUDRATE 1048576

#define BLE_SVC_UUID16 0xABC0     /* 16 Bit Service UUID */
#define BLE_SVC_CHR_UUID16 0xABC1 /* 16 Bit Service Characteristic UUID */
#define UART_QUEUE_LEN 64         // max messages queued

typedef struct
{
    uint8_t data[MSGLEN];
} uart_msg_t;

// Circular buffer
static uart_msg_t uart_queue[UART_QUEUE_LEN];
static volatile int uart_head = 0; // index to write
static volatile int uart_tail = 0; // index to read

static int server_gap_event(struct ble_gap_event *event, void *arg);
static int service_gatt_handler(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

static uint8_t own_addr_type;
static uint16_t ble_svc_gatt_read_val_handle;
static uint16_t server_conn_handle = 0;

// For random static address, 2 MSB bits of the first byte shall be 0b11.
// I.e. addr[5] shall be in the range of 0xC0 to 0xFF
static const uint8_t server_addr[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0xC0};
static const uint8_t client_addr[] = {0x10, 0x20, 0x30, 0x40, 0x50, 0xC0};

static const struct ble_gatt_svc_def new_ble_svc_gatt_defs[] = {
    {
        /* The Service */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SVC_UUID16),
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                /* The characteristic */
                .uuid = BLE_UUID16_DECLARE(BLE_SVC_CHR_UUID16),
                .access_cb = service_gatt_handler,
                .val_handle = &ble_svc_gatt_read_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                0, /* No more characteristics */
            },
        },
    },
    {
        0, /* No more services. */
    },
};

// Logs information about a connection to the console.
static void server_print_conn_desc(const struct ble_gap_conn_desc *desc)
{
    char addr[18];

    sprintf(addr, "%02x:%02x:%02x:%02x:%02x:%02x", desc->our_id_addr.val[5], desc->our_id_addr.val[4],
            desc->our_id_addr.val[3], desc->our_id_addr.val[2], desc->our_id_addr.val[1], desc->our_id_addr.val[0]);
    ESP_LOGI(TAG, " our_id_addr_type=%d our_id_addr=%s", desc->our_id_addr.type, addr);

    sprintf(addr, "%02x:%02x:%02x:%02x:%02x:%02x", desc->peer_id_addr.val[5], desc->peer_id_addr.val[4],
            desc->peer_id_addr.val[3], desc->peer_id_addr.val[2], desc->peer_id_addr.val[1], desc->peer_id_addr.val[0]);
    ESP_LOGI(TAG, " peer_id_addr_type=%d peer_id_addr=%s", desc->peer_id_addr.type, addr);

    ESP_LOGI(TAG, " conn_itvl=%d conn_latency=%d supervision_timeout=%d encrypted=%d authenticated=%d bonded=%d\n",
             desc->conn_itvl, desc->conn_latency, desc->supervision_timeout, desc->sec_state.encrypted,
             desc->sec_state.authenticated, desc->sec_state.bonded);
}

static void server_advertise(void)
{
    ESP_LOGI(TAG, "Advertising started");
    struct ble_hs_adv_fields fields = {0};
    const char *name = ble_svc_gap_device_name();

    // General discoverability and BLE-only (BR/EDR unsupported)
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    /* Set device name */
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    /* Set device tx power */
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.tx_pwr_lvl_is_present = 1;

    /* 16-bit service UUIDs (alert notifications) */
    fields.uuids16 = (ble_uuid16_t[]){BLE_UUID16_INIT(BLE_SVC_UUID16)};
    fields.uuids16_is_complete = 1;
    fields.num_uuids16 = 1;

    /* Set device LE role */
    fields.le_role = BLE_GAP_ROLE_SLAVE;
    fields.le_role_is_present = 1;

    int status = ble_gap_adv_set_fields(&fields);
    if (status == 0)
    {
        struct ble_gap_adv_params adv_params = {0};

        /* Set connetable and general discoverable mode */
        adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
        adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
        adv_params.filter_policy = BLE_HCI_ADV_FILT_NONE;

        /* Start advertising */
        status = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, server_gap_event, NULL);
        if (status == 0)
        {
            ESP_LOGI(TAG, "Advertising started!");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to start advertising, error code: %d", status);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Error setting advertisement data; status = %d\n", status);
    }
}

static int server_gap_event(struct ble_gap_event *event, void *)
{
    struct ble_gap_conn_desc desc;

    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT: /* A new connection was established or a connection attempt failed. */
        ESP_LOGI(TAG, "connection %s; status=%d ", event->connect.status == 0 ? "established" : "failed", event->connect.status);
        if (event->connect.status == 0)
        {
            assert(0 == ble_gap_conn_find(event->connect.conn_handle, &desc));
            server_print_conn_desc(&desc);

            struct ble_gap_upd_params conn_params = {0};
            conn_params.itvl_min = 24;             // 24*1.25ms = 30ms
            conn_params.itvl_max = 40;             // 40*1.25ms = 50ms
            conn_params.latency = 0;               // no slave latency
            conn_params.supervision_timeout = 100; // 100*10ms = 1s

            int rc = ble_gap_update_params(event->connect.conn_handle, &conn_params);
            if (rc != 0)
            {
                ESP_LOGE(TAG, "Failed to update connection params: %d", rc);
            }
        }
        else
        {
            /* Connection failed; resume advertising. */
            server_advertise();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect; reason=%d ", event->disconnect.reason);
        server_print_conn_desc(&event->disconnect.conn);
        server_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        server_advertise();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "advertise complete; reason=%d", event->adv_complete.reason);
        server_advertise();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "subscribe event; conn_handle=%d attr_handle=%d  reason=%d prevn=%d curn=%d previ=%d curi=%d\n",
                 event->subscribe.conn_handle, event->subscribe.attr_handle, event->subscribe.reason,
                 event->subscribe.prev_notify, event->subscribe.cur_notify,
                 event->subscribe.prev_indicate, event->subscribe.cur_indicate);

        if (event->subscribe.cur_notify)
        {
            server_conn_handle = event->subscribe.conn_handle;
            ESP_LOGI(TAG, "Client subscribed, notifications enabled");
        }
        else
        {
            server_conn_handle = BLE_HS_CONN_HANDLE_NONE; // unsubscribed
            ESP_LOGI(TAG, "Client unsubscribed, notifications disabled");
        }
        break;

    default:
        break;
    }

    return 0;
}

static void server_on_reset(int reason)
{
    ESP_LOGE(TAG, "Resetting state; reason=%d\n", reason);
}

static void server_on_sync(void)
{
    assert(0 == ble_hs_id_set_rnd(server_addr)); // Set random static address; BLE_ADDR_RANDOM

    assert(0 == ble_hs_util_ensure_addr(0));

    /* Figure out address type to use while advertising */
    assert(0 == ble_hs_id_infer_auto(0, &own_addr_type));

    uint8_t addr_val[6] = {0};
    assert(0 == ble_hs_id_copy_addr(own_addr_type, addr_val, NULL));

    printf("BLE Device Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
           addr_val[5], addr_val[4], addr_val[3], addr_val[2], addr_val[1], addr_val[0]);

    (void)client_addr;

    /* Begin advertising. */
    server_advertise();
}

/* Callback function for custom service */
static int service_gatt_handler(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *)
{
    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        ESP_LOGI(TAG, "Callback for read");
        break;

    default:
        ESP_LOGI(TAG, "\nDefault Callback");
        break;
    }

    return 0;
}

static void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *)
{
    char buf[BLE_UUID_STR_LEN] = {0};

    switch (ctxt->op)
    {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGI(TAG, "registered service %s with handle=%d\n", ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf), ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGI(TAG, "registering characteristic %s with def_handle=%d val_handle=%d\n",
                 ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf), ctxt->chr.def_handle, ctxt->chr.val_handle);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGI(TAG, "registering descriptor %s with handle=%d\n", ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf), ctxt->dsc.handle);
        break;

    default:
        assert(0);
        break;
    }
}

int gatt_svr_init(void)
{
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(new_ble_svc_gatt_defs);

    if (rc != 0)
    {
        return rc;
    }

    rc = ble_gatts_add_svcs(new_ble_svc_gatt_defs);
    return rc;
}

void uart_task(void *pvParameters)
{
    uint8_t buffer[MSGLEN];

    while (1)
    {
        // Blocking read from UART
        int n = uart_read_bytes(UART_NUM, buffer, MSGLEN, portMAX_DELAY);
        if (n == MSGLEN)
        {
            int next_head = (uart_head + 1) % UART_QUEUE_LEN;
            if (next_head != uart_tail) // queue not full
            {
                memcpy(uart_queue[uart_head].data, buffer, MSGLEN);
                uart_head = next_head;
            }
            else
            {
                // Queue full, drop message
                ESP_LOGW(TAG, "UART queue full, dropping message");
            }
        }
    }
}

void notify_task(void *pvParameters)
{
    while (1)
    {
        if (server_conn_handle != BLE_HS_CONN_HANDLE_NONE)
        {
            // Check if queue is not empty
            if (uart_tail != uart_head)
            {
                uint8_t *msg = uart_queue[uart_tail].data;
                struct os_mbuf *om = ble_hs_mbuf_from_flat(msg, MSGLEN);
                int rc = ble_gatts_notify_custom(server_conn_handle, ble_svc_gatt_read_val_handle, om);

                if (rc != 0)
                {
                    ESP_LOGE(TAG, "Notify failed: %d", rc);
                    os_mbuf_free_chain(om);
                    vTaskDelay(1); // back off
                }

                // Advance tail
                uart_tail = (uart_tail + 1) % UART_QUEUE_LEN;
            }
            else
            {
                // Queue empty, yield
                vTaskDelay(1);
            }
        }
        else
        {
            // Not connected, sleep
            vTaskDelay(5);
        }
    }
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO); // Enable INFO for all tags

    esp_err_t status = nvs_flash_init();
    if (status == ESP_ERR_NVS_NO_FREE_PAGES || status == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        status = nvs_flash_init();
    }
    ESP_ERROR_CHECK(status);

    ESP_ERROR_CHECK(nimble_port_init());
    ESP_ERROR_CHECK(esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P20));

    /* Initialize the NimBLE host configuration. */
    ble_hs_cfg.reset_cb = server_on_reset;
    ble_hs_cfg.sync_cb = server_on_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* Register custom service */
    assert(0 == gatt_svr_init());

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

    // --- Start UART producer task ---
    assert(pdTRUE == xTaskCreate(uart_task, "uart_task", 2048, NULL, 3, NULL));

    assert(pdTRUE == xTaskCreate(
                         notify_task,
                         "notify_task",
                         4096,
                         NULL,
                         2,
                         NULL));

    ESP_LOGI(TAG, "BLE Host Task Started");

    nimble_port_run(); /* This function will return only when nimble_port_stop() is executed */
    nimble_port_freertos_deinit();
}