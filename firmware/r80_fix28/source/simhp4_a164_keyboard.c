/*
 * simhP4 R14A - Tab5 A164 keyboard host bridge for MicroVAX II.
 *
 * Proven hardware/translation path extracted from the UNIX V0 Release Final
 * A164 implementation.  PDP-7/GRAPHICS-2 specific button handling is omitted.
 * The only guest-facing action is enqueueing an ASCII/control byte into the
 * host FIFO consumed by Open SIMH sim_os_poll_kbd().
 */

#include "simhp4_vax_keyboard.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SIMHP4_A164_ADDR             0x6Du
#define SIMHP4_A164_REG_INT_CFG      0x00u
#define SIMHP4_A164_REG_INT_STAT     0x01u
#define SIMHP4_A164_REG_EVENT_NUM    0x02u
#define SIMHP4_A164_REG_MODE         0x10u
#define SIMHP4_A164_REG_KEY_EVENT    0x20u
#define SIMHP4_A164_REG_FW_VERSION   0xFEu
#define SIMHP4_A164_MODE_NORMAL      0u
#define SIMHP4_A164_EVENT_EMPTY      0xFFu
#define SIMHP4_A164_ROWS             5u
#define SIMHP4_A164_COLS             14u
#define SIMHP4_A164_KEYS             70u
#define SIMHP4_A164_I2C_HZ           400000u
#define SIMHP4_A164_SDA_GPIO         GPIO_NUM_0
#define SIMHP4_A164_SCL_GPIO         GPIO_NUM_1

static const char *TAG = "SIMHP4_R14_A164";

static i2c_master_bus_handle_t s_a164_bus = NULL;
static i2c_master_dev_handle_t s_a164_dev = NULL;
static TaskHandle_t s_a164_task = NULL;

static volatile bool s_a164_present = false;
static bool s_a164_sym_down = false;
static bool s_a164_aa_down = false;
static bool s_a164_ctrl_down = false;
static bool s_a164_alt_down = false;
static unsigned s_a164_read_failures = 0;

int simhp4_a164_keyboard_present(void)
{
    return __atomic_load_n(&s_a164_present, __ATOMIC_ACQUIRE) ? 1 : 0;
}

static void simhp4_a164_publish_present(bool present)
{
    const bool old =
        __atomic_exchange_n(&s_a164_present, present, __ATOMIC_ACQ_REL);
    if (old != present)
        ESP_LOGI(TAG, "A164 %s", present ? "ATTACHED" : "DETACHED");
}

static bool simhp4_a164_i2c_init(void)
{
    if (s_a164_bus != NULL && s_a164_dev != NULL)
        return true;

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = (i2c_port_num_t)-1,
        .sda_io_num = SIMHP4_A164_SDA_GPIO,
        .scl_io_num = SIMHP4_A164_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = true,
            .allow_pd = false,
        },
    };

    i2c_master_bus_handle_t bus = NULL;
    esp_err_t e = i2c_new_master_bus(&bus_cfg, &bus);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "A164 I2C bus create failed: %s", esp_err_to_name(e));
        return false;
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SIMHP4_A164_ADDR,
        .scl_speed_hz = SIMHP4_A164_I2C_HZ,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = false,
        },
    };

    i2c_master_dev_handle_t dev = NULL;
    e = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    if (e != ESP_OK) {
        (void)i2c_del_master_bus(bus);
        ESP_LOGW(TAG, "A164 I2C add-device failed: %s", esp_err_to_name(e));
        return false;
    }

    s_a164_bus = bus;
    s_a164_dev = dev;
    return true;
}

static bool simhp4_a164_read(uint8_t reg, uint8_t *dst, size_t len)
{
    if (s_a164_dev == NULL || dst == NULL || len == 0)
        return false;

    return i2c_master_transmit_receive(
               s_a164_dev, &reg, 1, dst, len, 20) == ESP_OK;
}

static bool simhp4_a164_write8(uint8_t reg, uint8_t value)
{
    if (s_a164_dev == NULL)
        return false;

    const uint8_t tx[2] = {reg, value};
    return i2c_master_transmit(s_a164_dev, tx, sizeof(tx), 20) == ESP_OK;
}

static void simhp4_a164_reset_modifiers(void)
{
    s_a164_sym_down = false;
    s_a164_aa_down = false;
    s_a164_ctrl_down = false;
    s_a164_alt_down = false;
}

static bool simhp4_a164_detect_and_configure(void)
{
    if (!simhp4_a164_i2c_init())
        return false;

    esp_err_t e = ESP_FAIL;
    for (unsigned attempt = 0; attempt < 3u; ++attempt) {
        e = i2c_master_probe(s_a164_bus, SIMHP4_A164_ADDR, 20);
        if (e == ESP_OK)
            break;
        if (e == ESP_ERR_TIMEOUT || e == ESP_ERR_INVALID_STATE)
            (void)i2c_master_bus_reset(s_a164_bus);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (e != ESP_OK)
        return false;

    uint8_t fw = 0xffu;
    if (!simhp4_a164_read(SIMHP4_A164_REG_FW_VERSION, &fw, 1))
        return false;

    uint8_t mode_before = 0xffu;
    (void)simhp4_a164_read(SIMHP4_A164_REG_MODE, &mode_before, 1);

    if (!simhp4_a164_write8(
            SIMHP4_A164_REG_MODE, SIMHP4_A164_MODE_NORMAL))
        return false;

    vTaskDelay(pdMS_TO_TICKS(10));

    if (!simhp4_a164_write8(SIMHP4_A164_REG_INT_STAT, 0u))
        return false;
    if (!simhp4_a164_write8(SIMHP4_A164_REG_EVENT_NUM, 0u))
        return false;
    if (!simhp4_a164_write8(SIMHP4_A164_REG_INT_CFG, 0x01u))
        return false;

    uint8_t mode = 0xffu;
    uint8_t intcfg = 0xffu;
    if (!simhp4_a164_read(SIMHP4_A164_REG_MODE, &mode, 1) ||
        !simhp4_a164_read(SIMHP4_A164_REG_INT_CFG, &intcfg, 1) ||
        mode != SIMHP4_A164_MODE_NORMAL ||
        (intcfg & 0x01u) == 0u)
        return false;

    simhp4_a164_reset_modifiers();
    s_a164_read_failures = 0;
    simhp4_a164_publish_present(true);

    ESP_LOGI(TAG,
             "A164 detected fw=0x%02X mode %u->NORMAL intcfg=0x%02X",
             (unsigned)fw, (unsigned)mode_before, (unsigned)intcfg);
    return true;
}

/* Exact printable/control-byte mapping from the proven UNIX V0 A164 path.
 * Arrow keys remain non-character keys in R14A; the first bridge gate needs
 * ordinary terminal bytes (including Enter), not GRAPHICS-2 button aliases. */
static const uint8_t s_a164_ascii_base[SIMHP4_A164_KEYS] = {
    0x1B,'1','2','3','4','5','6','7','8','9','0','-','+',0x7F,
    '`','!','@','#','$','%','^','&','*','(',')','[',']','\\',
    '\t','q','w','e','r','t','y','u','i','o','p',';','\'',0x7F,
    0,0,'a','s','d','f','g','h','j','k','l',0,'_','\r',
    0,0,'z','x','c','v','b','n','m','.',0,0,0,' '
};

static const uint8_t s_a164_ascii_sym[SIMHP4_A164_KEYS] = {
    0x1B,'1','2','3','4','5','6','7','8','9','0','-','+',0x7F,
    '~','?','@','#','$','%','^','&','/','<','>','{','}','|',
    '\t','q','w','e','r','t','y','u','i','o','p',':','"',0x7F,
    0,0,'a','s','d','f','g','h','j','k','l',0,'=','\r',
    0,0,'z','x','c','v','b','n','m',',',0,0,0,' '
};

static void simhp4_a164_emit_key(unsigned idx, bool down)
{
    const unsigned k_sym  = 3u * SIMHP4_A164_COLS + 0u;
    const unsigned k_aa   = 3u * SIMHP4_A164_COLS + 1u;
    const unsigned k_ctrl = 4u * SIMHP4_A164_COLS + 0u;
    const unsigned k_alt  = 4u * SIMHP4_A164_COLS + 1u;

    if (idx == k_sym)  { s_a164_sym_down = down;  return; }
    if (idx == k_aa)   { s_a164_aa_down = down;   return; }
    if (idx == k_ctrl) { s_a164_ctrl_down = down; return; }
    if (idx == k_alt)  { s_a164_alt_down = down;  return; }

    if (!down || idx >= SIMHP4_A164_KEYS)
        return;

    uint8_t ch = s_a164_sym_down
               ? s_a164_ascii_sym[idx]
               : s_a164_ascii_base[idx];

    if (ch == 0)
        return;

    if (s_a164_aa_down && ch >= 'a' && ch <= 'z')
        ch = (uint8_t)(ch - 'a' + 'A');

    if (s_a164_ctrl_down) {
        uint8_t u = ch;
        if (u >= 'a' && u <= 'z')
            u = (uint8_t)(u - 'a' + 'A');
        if (u >= '@' && u <= '_')
            ch = (uint8_t)(u & 0x1Fu);
    }

    (void)s_a164_alt_down;

    if (!simhp4_vax_keyboard_enqueue(ch)) {
        ESP_LOGW(TAG,
                 "A164 -> VAX TTI FIFO full/unavailable ch=%02X drops=%lu",
                 (unsigned)ch,
                 (unsigned long)simhp4_vax_keyboard_drop_count());
    }
}

static void simhp4_a164_process_raw(uint8_t raw)
{
    if (raw == SIMHP4_A164_EVENT_EMPTY)
        return;

    const bool down = (raw & 0x80u) != 0u;
    const unsigned row = (raw >> 4) & 0x07u;
    const unsigned col = raw & 0x0Fu;

    if (row >= SIMHP4_A164_ROWS || col >= SIMHP4_A164_COLS)
        return;

    simhp4_a164_emit_key(row * SIMHP4_A164_COLS + col, down);
}

static bool simhp4_a164_drain(void)
{
    bool hit = false;

    for (unsigned n = 0; n < 32u; ++n) {
        uint8_t raw = SIMHP4_A164_EVENT_EMPTY;
        if (!simhp4_a164_read(
                SIMHP4_A164_REG_KEY_EVENT, &raw, 1)) {
            ++s_a164_read_failures;
            return false;
        }

        s_a164_read_failures = 0;

        if (raw == SIMHP4_A164_EVENT_EMPTY)
            break;

        hit = true;
        simhp4_a164_process_raw(raw);
    }

    if (hit)
        (void)simhp4_a164_write8(SIMHP4_A164_REG_INT_STAT, 0u);

    return true;
}

static void simhp4_a164_task(void *arg)
{
    (void)arg;
    unsigned tick10 = 0u;

    for (;;) {
        if (!simhp4_a164_keyboard_present()) {
            if ((tick10++ % 100u) == 0u)
                (void)simhp4_a164_detect_and_configure();

            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        (void)simhp4_a164_drain();

        if (s_a164_read_failures >= 10u) {
            simhp4_a164_reset_modifiers();
            s_a164_read_failures = 0;
            simhp4_a164_publish_present(false);
        }

        ++tick10;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

int simhp4_a164_keyboard_start(void)
{
    if (!simhp4_a164_i2c_init()) {
        ESP_LOGW(TAG, "A164 host unavailable");
        return 0;
    }

    (void)simhp4_a164_detect_and_configure();

    if (s_a164_task != NULL)
        return 1;

    const BaseType_t ok = xTaskCreatePinnedToCore(
        simhp4_a164_task,
        "simhp4_a164",
        4096,
        NULL,
        3,
        &s_a164_task,
        0);

    if (ok != pdPASS) {
        s_a164_task = NULL;
        ESP_LOGE(TAG, "A164 CPU0 task create failed");
        return 0;
    }

    ESP_LOGI(TAG,
             "A164 service READY CPU0 prio=3 direct KEY_EVENT/10ms present=%d",
             simhp4_a164_keyboard_present());
    return 1;
}
