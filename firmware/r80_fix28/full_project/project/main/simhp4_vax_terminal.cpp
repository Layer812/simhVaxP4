/*
 * simhP4 R14C - MicroVAX II local text-console presenter for M5Stack Tab5.
 *
 * Guest-visible VAX TTO semantics remain in upstream/Open SIMH code.
 * sim_os_putchar() only mirrors each output byte into a host FIFO.  This
 * low-priority CPU0 task consumes that FIFO and renders ordinary terminal
 * control characters on the already-initialized M5GFX display.
 *
 * No VAX device state, timing, RQ state, or media state is touched here.
 */

#include <M5Unified.h>

#include <cstdint>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "simhp4_vax_terminal.h"

static const char *TAG = "SIMHP4_R14C";
static TaskHandle_t s_terminal_task = nullptr;

struct TerminalGeometry {
    int x;
    int y;
    int w;
    int h;
    int cursor_x;
    int cursor_y;
    int cell_w;
    int line_h;
    float text_size;
};

static TerminalGeometry s_term = {};

static uint16_t term_bg()   { return M5.Display.color565(0, 4, 2); }
static uint16_t term_frame(){ return M5.Display.color565(0, 126, 56); }
static uint16_t term_text() { return M5.Display.color565(112, 255, 164); }
static uint16_t term_dim()  { return M5.Display.color565(28, 150, 78); }

static void terminal_newline()
{
    s_term.cursor_x = s_term.x + 6;
    s_term.cursor_y += s_term.line_h;

    const int bottom = s_term.y + s_term.h - 6;
    if (s_term.cursor_y + s_term.line_h > bottom) {
        M5.Display.setScrollRect(s_term.x,
                                 s_term.y,
                                 s_term.w,
                                 s_term.h,
                                 term_bg());
        M5.Display.scroll(0, -s_term.line_h);
        s_term.cursor_y -= s_term.line_h;
    }
}

static void terminal_clear()
{
    M5.Display.fillRect(s_term.x, s_term.y,
                        s_term.w, s_term.h, term_bg());
    s_term.cursor_x = s_term.x + 6;
    s_term.cursor_y = s_term.y + 6;
}

static void terminal_draw_char(uint8_t ch)
{
    if (s_term.cursor_x + s_term.cell_w >
        s_term.x + s_term.w - 6) {
        terminal_newline();
    }

    const int x = s_term.cursor_x;
    const int y = s_term.cursor_y;

    M5.Display.fillRect(x, y,
                        s_term.cell_w, s_term.line_h,
                        term_bg());
    M5.Display.setTextSize(s_term.text_size);
    M5.Display.setTextColor(term_text());
    M5.Display.drawChar(ch, x, y);
    s_term.cursor_x += s_term.cell_w;
}

static void terminal_putc(uint8_t ch)
{
    switch (ch) {
    case '\f':
        terminal_clear();
        break;
    case '\r':
        s_term.cursor_x = s_term.x + 6;
        break;
    case '\n':
        terminal_newline();
        break;
    case '\b':
    case 0x7f:
        if (s_term.cursor_x > s_term.x + 6) {
            s_term.cursor_x -= s_term.cell_w;
            M5.Display.fillRect(s_term.cursor_x,
                                s_term.cursor_y,
                                s_term.cell_w,
                                s_term.line_h,
                                term_bg());
        }
        break;
    case '\t': {
        const int col =
            (s_term.cursor_x - (s_term.x + 6)) / s_term.cell_w;
        const int spaces = 8 - (col & 7);
        for (int i = 0; i < spaces; ++i)
            terminal_draw_char(' ');
        break;
    }
    default:
        if (ch >= 0x20 && ch < 0x7f)
            terminal_draw_char(ch);
        break;
    }
}

static void terminal_draw_chrome()
{
    const int W = M5.Display.width();
    const int H = M5.Display.height();

    M5.Display.fillScreen(term_bg());

    M5.Display.setTextColor(term_text());
    M5.Display.setTextSize(2.4f);
    M5.Display.drawString("simhP4  MICROVAX II / KA630 CONSOLE", 20, 12);

    M5.Display.setTextColor(term_dim());
    M5.Display.setTextSize(1.45f);
    M5.Display.drawString(
        "RQ0 READ-ONLY   A164 KEYBOARD READY   press Enter if KA630 waits",
        22, 50);

    s_term.x = 18;
    s_term.y = 84;
    s_term.w = W - 36;
    s_term.h = H - 102;
    s_term.text_size = 2.15f;

    M5.Display.drawRect(s_term.x - 2, s_term.y - 2,
                        s_term.w + 4, s_term.h + 4,
                        term_frame());
    M5.Display.fillRect(s_term.x, s_term.y,
                        s_term.w, s_term.h, term_bg());

    M5.Display.setTextSize(s_term.text_size);
    s_term.cell_w = M5.Display.textWidth("M") + 1;
    s_term.line_h = M5.Display.fontHeight() + 3;
    if (s_term.cell_w < 8)
        s_term.cell_w = 8;
    if (s_term.line_h < 14)
        s_term.line_h = 14;

    s_term.cursor_x = s_term.x + 6;
    s_term.cursor_y = s_term.y + 6;

    M5.Display.setScrollRect(s_term.x,
                             s_term.y,
                             s_term.w,
                             s_term.h,
                             term_bg());
    M5.Display.setTextScroll(false);

    ESP_LOGI(TAG,
             "TERMINAL SCREEN ready W=%d H=%d pane=%d,%d %dx%d cell=%dx%d",
             W, H,
             s_term.x, s_term.y, s_term.w, s_term.h,
             s_term.cell_w, s_term.line_h);
}

static void terminal_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "TERMINAL TASK CPU%d prio=%u",
             xPortGetCoreID(),
             (unsigned)uxTaskPriorityGet(nullptr));

    for (;;) {
        uint8_t ch = 0;
        if (!simhp4_vax_terminal_dequeue_timed(&ch, 50))
            continue;

        terminal_putc(ch);

        /* Drain a bounded burst without blocking so normal console output
         * does not build up while keeping this presenter cooperative. */
        for (unsigned i = 0; i < 127u; ++i) {
            if (!simhp4_vax_terminal_dequeue_timed(&ch, 0))
                break;
            terminal_putc(ch);
        }
    }
}

extern "C" int simhp4_vax_terminal_start(void)
{
    if (!simhp4_vax_terminal_fifo_init()) {
        ESP_LOGE(TAG, "TTO FIFO allocation failed");
        return 0;
    }

    terminal_draw_chrome();

    if (s_terminal_task != nullptr)
        return 1;

    const BaseType_t ok = xTaskCreatePinnedToCore(
        terminal_task,
        "simhp4_vax_term",
        4096,
        nullptr,
        1,
        &s_terminal_task,
        0);

    if (ok != pdPASS) {
        s_terminal_task = nullptr;
        ESP_LOGE(TAG, "terminal presenter task create failed");
        return 0;
    }

    ESP_LOGI(TAG,
             "TERMINAL READY fifo=2048 presenter=1 CPU0 prio=1 drops=%lu",
             (unsigned long)simhp4_vax_terminal_drop_count());
    return 1;
}
