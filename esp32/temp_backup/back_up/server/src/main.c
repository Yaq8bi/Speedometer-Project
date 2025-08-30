#include "esp_log.h"
#include "nvs_flash.h"
#include <stdbool.h>

// Nimble (BLE) Headers
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// Driver Headers
#include "driver/uart.h"
#include "driver/gpio.h"
#include "led_strip.h"
#include "freertos/task.h"

#define TAG "BLE_UART_BRIDGE"
#define DEVICE_NAME "BLE_SERVER_BRIDGE"

// Note: Last byte (0xD0) starts with '11' in binary (11010000), making it a valid static address.
static const uint8_t server_addr[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0xD0};

// --- UART Defines ---
#define UART_NUM UART_NUM_1
#define UART_TX_PIN GPIO_NUM_17
#define UART_RX_PIN GPIO_NUM_18
#define UART_BUF_SIZE 128 // Increased buffer size for robustness

// --- BLE Defines ---
#define BLE_SVC_UUID16 0xABC0
#define BLE_SVC_CHR_UUID16 0xABC1
static uint8_t own_addr_type;
static uint16_t conn_handle_global; // Store the connection handle

// --- LED Defines ---
#define ONBOARD_LED_GPIO 48
#define HALF_BRIGHTNESS 12 // Slightly brighter for clarity
static led_strip_handle_t led_strip;
enum on_board_led_state
{
    LED_OFF = 0,
    LED_BLUE, // Connected
    LED_RED,  // Data Received
    LED_GREEN // Advertising
};

// --- Forward Declarations ---
static void server_advertise(void);
static int server_gap_event(struct ble_gap_event *event, void *arg);

// --- LED Control Function ---
static void configure_led(void)
{
    ESP_LOGI(TAG, "Configuring on-board LED...");
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };
    led_strip_config_t strip_config = {
        .strip_gpio_num = ONBOARD_LED_GPIO,
        .max_leds = 1,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip);
}

static void set_led_color(enum on_board_led_state led_state)
{
    switch (led_state)
    {
    case LED_GREEN: // Advertising
        ESP_LOGI(TAG, "LED State: GREEN (Advertising)");
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 0, HALF_BRIGHTNESS, 0));
        break;
    case LED_BLUE: // Connected
        ESP_LOGI(TAG, "LED State: BLUE (Connected)");
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 0, 0, HALF_BRIGHTNESS));
        break;
    case LED_RED: // Data Received
        ESP_LOGI(TAG, "LED State: RED (Data Received)");
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, HALF_BRIGHTNESS, 0, 0));
        break;
    case LED_OFF:
    default:
        ESP_LOGI(TAG, "LED State: OFF");
        led_strip_clear(led_strip);
        break;
    }
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));
}

// --- UART Initialization ---
static void init_uart(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_LOGI(TAG, "Configuring UART%d (TX:%d, RX:%d)", UART_NUM, UART_TX_PIN, UART_RX_PIN);
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

// --- Core BLE Logic: GATT Handler ---
static int service_gatt_handler(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
    {
        uint16_t data_len = OS_MBUF_PKTLEN(ctxt->om);
        if (data_len <= 0)
        {
            return 0;
        }

        // Set LED to RED to indicate data reception
        set_led_color(LED_RED);

        // Allocate a buffer on the stack to hold the received data
        uint8_t buffer[data_len];
        os_mbuf_copydata(ctxt->om, 0, data_len, buffer);

        // Write the received data to the UART
        uart_write_bytes(UART_NUM, (const char *)buffer, data_len);
        ESP_LOGI(TAG, "Received %d bytes via BLE, wrote to UART", data_len);

        // After processing, set LED back to BLUE to indicate 'connected and idle'
        // This provides a "flash" effect without using a delay.
        set_led_color(LED_BLUE);

        return 0;
    }
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

// --- BLE Service and Characteristic Definitions ---
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SVC_UUID16),
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = BLE_UUID16_DECLARE(BLE_SVC_CHR_UUID16),
                .access_cb = service_gatt_handler,
                // Server only needs to receive data, so only WRITE flag is needed.
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {0}, /* No more characteristics */
        },
    },
    {0}, /* No more services */
};

// --- Standard BLE Server Callbacks ---
static int server_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "BLE GAP Event: Client Connected; status=%d", event->connect.status);
        if (event->connect.status == 0)
        {
            conn_handle_global = event->connect.conn_handle;
            set_led_color(LED_BLUE); // Set LED to blue on connection
        }
        else
        {
            // Connection failed, start advertising again
            server_advertise();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE GAP Event: Client Disconnected; reason=%d", event->disconnect.reason);
        conn_handle_global = 0; // Invalidate connection handle
        set_led_color(LED_OFF); // Turn off LED
        // Start advertising again to allow new connections
        server_advertise();
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU update event; conn_handle=%d cid=%d mtu=%d", event->mtu.conn_handle, event->mtu.channel_id, event->mtu.value);
        break;
    }
    return 0;
}

static void server_advertise(void)
{
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    fields.uuids16 = (ble_uuid16_t[]){BLE_UUID16_INIT(BLE_SVC_UUID16)};
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    ESP_ERROR_CHECK(ble_gap_adv_set_fields(&fields));

    struct ble_gap_adv_params adv_params = {.conn_mode = BLE_GAP_CONN_MODE_UND, .disc_mode = BLE_GAP_DISC_MODE_GEN};
    int rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, server_gap_event, NULL);
    if (rc == 0)
    {
        ESP_LOGI(TAG, "Successfully started advertising");
        set_led_color(LED_GREEN); // Set LED to green to indicate advertising
    }
    else
    {
        ESP_LOGE(TAG, "Failed to start advertising; rc=%d", rc);
    }
}

static void server_on_reset(int reason)
{
    ESP_LOGE(TAG, "Resetting state; reason=%d", reason);
    set_led_color(LED_OFF);
}

static void server_on_sync(void)
{
    int rc;
    ESP_LOGI(TAG, "BLE Stack synced and ready.");

    // Set our hardcoded random static address
    rc = ble_hs_id_set_rnd(server_addr);
    assert(rc == 0);

    // We must now use a random address type
    own_addr_type = BLE_ADDR_RANDOM;

    // Log the address to be sure
    char addr_str[18];
    sprintf(addr_str, "%02X:%02X:%02X:%02X:%02X:%02X",
            server_addr[5], server_addr[4], server_addr[3],
            server_addr[2], server_addr[1], server_addr[0]);
    ESP_LOGI(TAG, "Set static address to: %s", addr_str);

    // Start advertising with our new static address
    server_advertise();
}

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];
    switch (ctxt->op)
    {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGI(TAG, "Registered service %s with handle=%d", ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf), ctxt->svc.handle);
        break;
    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGI(TAG, "Registering characteristic %s with def_handle=%d val_handle=%d", ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf), ctxt->chr.def_handle, ctxt->chr.val_handle);
        break;
    default:
        break;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "--- BLE to UART Bridge SERVER Starting Up ---");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize peripherals
    configure_led();
    init_uart();

    // Initialize NimBLE stack
    ESP_ERROR_CHECK(nimble_port_init());

    // Configure BLE host callbacks
    ble_hs_cfg.reset_cb = server_on_reset;
    ble_hs_cfg.sync_cb = server_on_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // Initialize standard BLE services
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // Add our custom GATT service
    ESP_ERROR_CHECK(ble_gatts_count_cfg(gatt_svcs));
    ESP_ERROR_CHECK(ble_gatts_add_svcs(gatt_svcs));

    // Set the device name
    ESP_ERROR_CHECK(ble_svc_gap_device_name_set(DEVICE_NAME));

    // Start the NimBLE host task
    ESP_LOGI(TAG, "Starting NimBLE Host Task...");
    nimble_port_run();
    nimble_port_freertos_deinit();
}