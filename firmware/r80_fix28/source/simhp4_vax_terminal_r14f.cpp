/*
 * simhP4 R14D - MicroVAX II local VT100-like console presenter for M5Stack Tab5.
 *
 * Exact R14C baseline retained:
 *   VAX TTO -> private FIFO -> low-priority CPU1 M5GFX presenter (RC14).
 *
 * R14D host-only additions:
 *   1. Consume ANSI/DEC escape sequences instead of drawing them literally.
 *   2. Answer VT100 DA/DECID and basic DSR queries through the already-proven
 *      host keyboard FIFO so KA630 console terminal identification can finish.
 *   3. When A164 is present, rotate the already-initialized Tab5 display by
 *      180 degrees relative to its native orientation, matching the proven
 *      PDP-7 local-seat behavior.  Hotplug presence changes are polled and
 *      redraw the terminal chrome in the corresponding orientation.
 *
 * No VAX device state, timing, RQ state, media state, or scheduler semantics
 * are changed here.
 */

#include <M5Unified.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "simhp4_vax_keyboard.h"
#include "simhp4_vax_terminal.h"
#include "simh_wifi_boot_gate.h"
#include "simhp4_wifi_overlay.h"

static const char *TAG = "SIMHP4_R14F";
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
/* RC14 FIX1 (native link): the 8000-byte terminal text shadow is not DMA
 * memory; placing it in internal .bss unnecessarily crowds ESP32-P4 DRAM.
 * Allocate once from external PSRAM before any terminal paint or task start.
 * This leaves guest console content/VT100 behavior entirely unchanged. */
static char (*s_wifi_console_shadow)[160] = nullptr;
static bool wifi_shadow_allocate(void) {
    if (s_wifi_console_shadow) return true;
    void* p = heap_caps_calloc(50u, 160u, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) {
        ESP_LOGW(TAG, "RC14_FIX1 PSRAM shadow unavailable; internal fallback");
        p = heap_caps_calloc(50u, 160u, MALLOC_CAP_8BIT);
    }
    s_wifi_console_shadow = static_cast<char (*)[160]>(p);
    if (!s_wifi_console_shadow) {
        ESP_LOGE(TAG, "RC14_FIX1 terminal shadow allocation failed");
        return false;
    }
    ESP_LOGI(TAG, "SIMHP4_RC14_FIX1_SHADOW_ALLOC bytes=8000");
    return true;
}
static void wifi_shadow_blank(void) {
    for (int r=0; r<50; ++r) std::memset(s_wifi_console_shadow[r], ' ', 160);
}
static void wifi_shadow_scroll(int rows) {
    if(rows>50)rows=50;
    if(rows<=0)return;
    if(rows>1)std::memmove(s_wifi_console_shadow[0],s_wifi_console_shadow[1],(size_t)(rows-1)*160);
    std::memset(s_wifi_console_shadow[rows-1],' ',160);
}
static uint8_t s_base_rotation = 0;
static int s_last_a164_present = -1;
static int s_a164_candidate = -1;
static uint32_t s_a164_candidate_ms = 0;

static int s_saved_cursor_x = 0;
static int s_saved_cursor_y = 0;
/* RELEASE_UI: DEC scrolling margins, cursor and underline presenter. */
static int s_scroll_top=1,s_scroll_bottom=0;
static bool s_cursor_enabled=true,s_cursor_painted=false;
static bool s_dec_autowrap=true;
static int s_cursor_underline_x=0,s_cursor_underline_y=0;
static uint32_t s_cursor_last_toggle=0;


enum ParserState : uint8_t {
    PS_NORMAL = 0,
    PS_ESC,
    PS_CSI,
    PS_ESC_INTERMEDIATE,
};

static ParserState s_parser = PS_NORMAL;
static char s_csi[40] = {};
static size_t s_csi_len = 0;

static uint16_t term_bg()   { return M5.Display.color565(0, 4, 2); }
static uint16_t term_frame(){ return M5.Display.color565(0, 126, 56); }
static uint16_t term_text() { return M5.Display.color565(112, 255, 164); }
static uint16_t term_dim()  { return M5.Display.color565(28, 150, 78); }

static int terminal_cols()
{
    if (s_term.cell_w <= 0)
        return 1;
    const int n = (s_term.w - 12) / s_term.cell_w;
    /* 4.3BSD rogue uses an 80-column field; keep emulator and TERMCAP
     * geometry identical instead of allowing an 82-column host wrap. */
    return n >= 80 ? 80 : (n > 0 ? n : 1);
}

static int terminal_rows()
{
    if (s_term.line_h <= 0)
        return 1;
    const int n = (s_term.h - 12) / s_term.line_h;
    return n > 0 ? n : 1;
}

static int terminal_col()
{
    if (s_term.cell_w <= 0)
        return 1;
    int col = (s_term.cursor_x - (s_term.x + 6)) / s_term.cell_w + 1;
    if (col < 1) col = 1;
    return col;
}

static int terminal_row()
{
    if (s_term.line_h <= 0)
        return 1;
    int row = (s_term.cursor_y - (s_term.y + 6)) / s_term.line_h + 1;
    if (row < 1) row = 1;
    return row;
}

static void terminal_set_cursor_rc(int row, int col)
{
    if (row < 1) row = 1;
    if (col < 1) col = 1;
    const int rows = terminal_rows();
    const int cols = terminal_cols();
    if (row > rows) row = rows;
    if (col > cols) col = cols;
    s_term.cursor_x = s_term.x + 6 + (col - 1) * s_term.cell_w;
    s_term.cursor_y = s_term.y + 6 + (row - 1) * s_term.line_h;
}

static void terminal_cursor_erase()
{
    if(!s_cursor_painted)return;
    s_cursor_painted=false;
    if(!simhp4_wifi_overlay_is_open())
        M5.Display.fillRect(s_cursor_underline_x,s_cursor_underline_y, s_term.cell_w-2,2,term_bg());
}
static void terminal_cursor_tick()
{
    if(simhp4_wifi_overlay_is_open()){
        s_cursor_painted=false;return;
    }
    const uint32_t now=(uint32_t)(xTaskGetTickCount()*portTICK_PERIOD_MS);
    if(!s_cursor_enabled){terminal_cursor_erase();return;}
    if(s_cursor_painted && (uint32_t)(now-s_cursor_last_toggle)>=550u){
        terminal_cursor_erase();s_cursor_last_toggle=now;return;
    }
    if(!s_cursor_painted && (uint32_t)(now-s_cursor_last_toggle)>=350u){
        s_cursor_underline_x=s_term.cursor_x+1;
        s_cursor_underline_y=s_term.cursor_y+s_term.line_h-3;
        M5.Display.fillRect(s_cursor_underline_x,s_cursor_underline_y,s_term.cell_w-2,2,term_text());
        s_cursor_painted=true;s_cursor_last_toggle=now;
    }
}
static void terminal_draw_cell(int row,int col)
{
    if(row<0 || row>=terminal_rows() || col<0 || col>=terminal_cols())return;
    const int x=s_term.x+6+col*s_term.cell_w,y=s_term.y+6+row*s_term.line_h;
    if(simhp4_wifi_overlay_is_open())return;
    M5.Display.fillRect(x,y,s_term.cell_w,s_term.line_h,term_bg());
    const unsigned char ch=(unsigned char)s_wifi_console_shadow[row][col];
    if(ch>=0x20&&ch<=0x7e){
        M5.Display.setTextSize(s_term.text_size);M5.Display.setTextColor(term_text());
        M5.Display.drawChar(ch,x,y);
    }
}
static void terminal_erase_line_range(int row,int left,int right)
{
    const int cols=terminal_cols();
    if(row<1||row>terminal_rows())return;
    if(left<1)left=1;
    if(right>cols)right=cols;
    for(int c=left;c<=right;c++)
        if(row-1<50 && c-1<160)s_wifi_console_shadow[row-1][c-1]=' ';
    if(!simhp4_wifi_overlay_is_open() && right>=left)
        M5.Display.fillRect(s_term.x+6+(left-1)*s_term.cell_w,
            s_term.y+6+(row-1)*s_term.line_h,
            (right-left+1)*s_term.cell_w,s_term.line_h,term_bg());
}
static void terminal_scroll_region(int top,int bottom,int lines,bool upward)
{
    const int rows=terminal_rows(),cols=terminal_cols();
    if(top<1)top=1;
    if(bottom>rows)bottom=rows;
    if(top>=bottom)return;
    if(lines>bottom-top+1)lines=bottom-top+1;
    if(lines<1)return;
    for(int i=0;i<lines;i++){
        if(upward){
            for(int r=top-1;r<bottom-1;r++)
                std::memmove(s_wifi_console_shadow[r],s_wifi_console_shadow[r+1],160);
            std::memset(s_wifi_console_shadow[bottom-1],' ',160);
        }else{
            for(int r=bottom-1;r>top-1;r--)
                std::memmove(s_wifi_console_shadow[r],s_wifi_console_shadow[r-1],160);
            std::memset(s_wifi_console_shadow[top-1],' ',160);
        }
    }
    /* Original M5GFX scroll path is retained for the usual full terminal.
     * For DEC partial scroll margins, repaint only the affected row range. */
    if(upward && lines==1 && top==1 && bottom==rows){
        if(!simhp4_wifi_overlay_is_open()){
            M5.Display.setScrollRect(s_term.x,s_term.y,s_term.w,s_term.h,term_bg());
            M5.Display.scroll(0,-s_term.line_h);
        }
    }else for(int r=top-1;r<bottom;r++)for(int c=0;c<cols;c++)terminal_draw_cell(r,c);
}
static int terminal_bottom_margin(){return s_scroll_bottom>0?s_scroll_bottom:terminal_rows();}
static void terminal_linefeed(bool carriage_return)
{
    const int row=terminal_row();
    const int col=terminal_col();
    if(row==terminal_bottom_margin()){
        terminal_scroll_region(s_scroll_top,terminal_bottom_margin(),1,true);
        terminal_set_cursor_rc(row,carriage_return?1:col);
    }else if(row<terminal_rows())terminal_set_cursor_rc(row+1,carriage_return?1:col);
    else terminal_set_cursor_rc(row,carriage_return?1:col);
}
static void terminal_newline(){terminal_linefeed(true);}
static void terminal_clear()
{
    wifi_shadow_blank();
    if (!simhp4_wifi_overlay_is_open())
        M5.Display.fillRect(s_term.x,s_term.y,s_term.w,s_term.h,term_bg());
    terminal_set_cursor_rc(1,1);
}
static void terminal_clear_eol(){terminal_erase_line_range(terminal_row(),terminal_col(),terminal_cols());}
static void terminal_clear_display(int mode)
{
    const int r=terminal_row(),c=terminal_col();
    if(mode==2 || mode==3){
        wifi_shadow_blank();
        if(!simhp4_wifi_overlay_is_open())M5.Display.fillRect(s_term.x,s_term.y,s_term.w,s_term.h,term_bg());
        return; /* VT100 ED2 does NOT move the cursor. */
    }
    if(mode==0){
        terminal_erase_line_range(r,c,terminal_cols());
        for(int y=r+1;y<=terminal_rows();y++)terminal_erase_line_range(y,1,terminal_cols());
    }else if(mode==1){
        for(int y=1;y<r;y++)terminal_erase_line_range(y,1,terminal_cols());
        terminal_erase_line_range(r,1,c);
    }
}
static void terminal_clear_line(int mode)
{
    const int r=terminal_row(),c=terminal_col();
    if(mode==0)terminal_erase_line_range(r,c,terminal_cols());
    else if(mode==1)terminal_erase_line_range(r,1,c);
    else if(mode==2)terminal_erase_line_range(r,1,terminal_cols());
}

static void terminal_draw_char(uint8_t ch)
{
    const int last_x = s_term.x + 6 + (terminal_cols()-1)*s_term.cell_w;
    if (s_term.cursor_x > last_x) {
        if(s_dec_autowrap)terminal_newline();
        else s_term.cursor_x=last_x;
    }

    const int x = s_term.cursor_x;
    const int y = s_term.cursor_y;

    const int row=terminal_row()-1,col=terminal_col()-1;
    if(row>=0&&row<50&&col>=0&&col<160)
        s_wifi_console_shadow[row][col]=(char)ch;
    if (!simhp4_wifi_overlay_is_open()) {
        M5.Display.fillRect(x,y,s_term.cell_w,s_term.line_h,term_bg());
        M5.Display.setTextSize(s_term.text_size);
        M5.Display.setTextColor(term_text());
        M5.Display.drawChar(ch,x,y);
    }
    if(s_dec_autowrap || x<last_x)
        s_term.cursor_x += s_term.cell_w;
}

static void terminal_plain_putc(uint8_t ch)
{
    switch (ch) {
    case '\a':
        break;
    case '\f':
        terminal_clear();
        break;
    case '\r':
        s_term.cursor_x = s_term.x + 6;
        break;
    case '\n':
        /* VT100 LF/IND preserves the column; only CR or NEL resets it.
         * Full-screen vi uses LF for vertical cursor movements. */
        terminal_linefeed(false);
        break;
    case '\b':
        /* BS moves the cursor only; BSD echoes BS/SP/BS to erase text. */
        terminal_set_cursor_rc(terminal_row(),terminal_col()-1);
        break;
    case 0x7f:
        /* DEL is a guest keyboard control, not a printable output glyph. */
        break;
    case '\t': {
        const int col = terminal_col() - 1;
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

static void terminal_reply_bytes(const char *s, const char *why)
{
    unsigned sent = 0;
    for (const unsigned char *p =
             reinterpret_cast<const unsigned char *>(s);
         *p != 0;
         ++p) {
        if (!simhp4_vax_keyboard_enqueue(*p)) {
            ESP_LOGW(TAG,
                     "VT reply enqueue failed why=%s sent=%u drops=%lu",
                     why,
                     sent,
                     (unsigned long)simhp4_vax_keyboard_drop_count());
            return;
        }
        ++sent;
    }
    ESP_LOGI(TAG, "VT reply %s bytes=%u", why, sent);
}

static void terminal_reply_da(const char *query)
{
    /* SIMHP4_R14F_ENGLISH_VT100_NO_OPTIONS
     * Report the least-capable classic VT100 identity.  ESC[?1;0c is the
     * VT100 "No options" DA response.  In particular, do not advertise AVO
     * or any later DEC terminal capability that could imply MCS support.
     * The KA630 console should therefore select its English-only path. */
    terminal_reply_bytes("\x1b[?1;0c", query);
}

static void terminal_reply_dsr_ok()
{
    terminal_reply_bytes("\x1b[0n", "DSR-STATUS");
}

static void terminal_reply_cpr()
{
    char buf[32];
    const int row = terminal_row();
    const int col = terminal_col();
    std::snprintf(buf, sizeof(buf), "\x1b[%d;%dR", row, col);
    terminal_reply_bytes(buf, "DSR-CURSOR");
}

static int csi_param_at(int index, int default_value)
{
    int current_index = 0;
    int value = 0;
    bool have = false;
    for (size_t i = 0; i <= s_csi_len; ++i) {
        const char c = (i < s_csi_len) ? s_csi[i] : ';';
        if (c >= '0' && c <= '9') {
            value = value * 10 + (c - '0');
            have = true;
            continue;
        }
        if (c == ';') {
            if (current_index == index)
                return have ? value : default_value;
            ++current_index;
            value = 0;
            have = false;
            continue;
        }
        /* Private prefixes such as '?' and '>' are ignored here. */
    }
    return default_value;
}

static bool csi_is_private(char prefix)
{
    return s_csi_len > 0 && s_csi[0] == prefix;
}

static bool csi_is_da_query()
{
    if (s_csi_len == 0)
        return true;
    if (s_csi_len == 1 && s_csi[0] == '0')
        return true;
    return false;
}

static void terminal_handle_csi(uint8_t final)
{
    switch (final) {
    case 'c':
        if (!csi_is_private('?') && !csi_is_private('>') && csi_is_da_query())
            terminal_reply_da("DA-CSI");
        break;

    case 'n': {
        const int p = csi_param_at(0, 0);
        if (p == 5)
            terminal_reply_dsr_ok();
        else if (p == 6)
            terminal_reply_cpr();
        break;
    }

    case 'H':
    case 'f': {
        const int row = csi_param_at(0, 1);
        const int col = csi_param_at(1, 1);
        terminal_set_cursor_rc(row, col);
        break;
    }

    case 'A': {
        int n = csi_param_at(0, 1); if (n < 1) n = 1;
        terminal_set_cursor_rc(terminal_row() - n, terminal_col());
        break;
    }
    case 'B': {
        int n = csi_param_at(0, 1); if (n < 1) n = 1;
        terminal_set_cursor_rc(terminal_row() + n, terminal_col());
        break;
    }
    case 'C': {
        int n = csi_param_at(0, 1); if (n < 1) n = 1;
        terminal_set_cursor_rc(terminal_row(), terminal_col() + n);
        break;
    }
    case 'D': {
        int n = csi_param_at(0, 1); if (n < 1) n = 1;
        terminal_set_cursor_rc(terminal_row(), terminal_col() - n);
        break;
    }

    case 'J':
        terminal_clear_display(csi_param_at(0,0));
        break;
    case 'K':
        terminal_clear_line(csi_param_at(0,0));
        break;
    case 'r': {
        int top=csi_param_at(0,1),bottom=csi_param_at(1,terminal_rows());
        if(top<1)top=1;
        if(bottom>terminal_rows())bottom=terminal_rows();
        if(top<bottom){s_scroll_top=top;s_scroll_bottom=bottom;terminal_set_cursor_rc(1,1);}
        break;
    }
    case 's':
        s_saved_cursor_x=s_term.cursor_x;s_saved_cursor_y=s_term.cursor_y;break;
    case 'u':
        s_term.cursor_x=s_saved_cursor_x;s_term.cursor_y=s_saved_cursor_y;break;
    case 'h':
    case 'l':
        if(csi_is_private('?')) {
            const int p=csi_param_at(0,0);
            if(p==25)s_cursor_enabled=(final=='h');
            if(p==7)s_dec_autowrap=(final=='h');
        }
        break;
    case 'L': {
        const int n=csi_param_at(0,1);
        if(terminal_row()>=s_scroll_top && terminal_row()<=terminal_bottom_margin())
            terminal_scroll_region(terminal_row(),terminal_bottom_margin(),n,false);
        break;
    }
    case 'M': {
        const int n=csi_param_at(0,1);
        if(terminal_row()>=s_scroll_top && terminal_row()<=terminal_bottom_margin())
            terminal_scroll_region(terminal_row(),terminal_bottom_margin(),n,true);
        break;
    }
    case 'P': {
        const int row=terminal_row()-1,col=terminal_col()-1,cols=terminal_cols();
        int n=csi_param_at(0,1);if(n<1)n=1;if(n>cols-col)n=cols-col;
        for(int c=col;c<cols-n;c++)s_wifi_console_shadow[row][c]=s_wifi_console_shadow[row][c+n];
        for(int c=cols-n;c<cols;c++)s_wifi_console_shadow[row][c]=' ';
        for(int c=col;c<cols;c++)terminal_draw_cell(row,c);
        break;
    }
    case '@': {
        const int row=terminal_row()-1,col=terminal_col()-1,cols=terminal_cols();
        int n=csi_param_at(0,1);if(n<1)n=1;if(n>cols-col)n=cols-col;
        for(int c=cols-1;c>=col+n;c--)s_wifi_console_shadow[row][c]=s_wifi_console_shadow[row][c-n];
        for(int c=col;c<col+n;c++)s_wifi_console_shadow[row][c]=' ';
        for(int c=col;c<cols;c++)terminal_draw_cell(row,c);
        break;
    }
    case 'm':
    case 'g':
    case 'q':
        break;
    default:
        break;
    }
}

static void terminal_parser_reset()
{
    s_parser = PS_NORMAL;
    s_csi_len = 0;
    s_csi[0] = '\0';
}

static void terminal_putc(uint8_t ch)
{
    if (s_parser == PS_NORMAL) {
        if (ch == 0x1b) {
            s_parser = PS_ESC;
            return;
        }
        if (ch == 0x9b) {
            s_parser = PS_CSI;
            s_csi_len = 0;
            return;
        }
        terminal_plain_putc(ch);
        return;
    }

    if (s_parser == PS_ESC) {
        switch (ch) {
        case '[':
            s_parser = PS_CSI;
            s_csi_len = 0;
            return;
        case 'Z':
            terminal_reply_da("DA-DECID");
            terminal_parser_reset();
            return;
        case 'D':
            terminal_linefeed(false);
            terminal_parser_reset();
            return;
        case 'E':
            terminal_newline();
            s_term.cursor_x = s_term.x + 6;
            terminal_parser_reset();
            return;
        case 'M':
            if(terminal_row()==s_scroll_top)
                terminal_scroll_region(s_scroll_top,terminal_bottom_margin(),1,false);
            else terminal_set_cursor_rc(terminal_row() - 1, terminal_col());
            terminal_parser_reset();
            return;
        case '7':
            s_saved_cursor_x = s_term.cursor_x;
            s_saved_cursor_y = s_term.cursor_y;
            terminal_parser_reset();
            return;
        case '8':
            s_term.cursor_x = s_saved_cursor_x;
            s_term.cursor_y = s_saved_cursor_y;
            terminal_parser_reset();
            return;
        case 'c':
            terminal_clear();
            terminal_parser_reset();
            return;
        case '(':
        case ')':
        case '*':
        case '+':
        case '#':
            s_parser = PS_ESC_INTERMEDIATE;
            return;
        case '=':
        case '>':
            terminal_parser_reset();
            return;
        default:
            terminal_parser_reset();
            return;
        }
    }

    if (s_parser == PS_ESC_INTERMEDIATE) {
        terminal_parser_reset();
        return;
    }

    if (s_parser == PS_CSI) {
        if (ch >= 0x40 && ch <= 0x7e) {
            s_csi[s_csi_len] = '\0';
            terminal_handle_csi(ch);
            terminal_parser_reset();
            return;
        }

        if (s_csi_len + 1 < sizeof(s_csi)) {
            s_csi[s_csi_len++] = static_cast<char>(ch);
            s_csi[s_csi_len] = '\0';
        } else {
            ESP_LOGW(TAG, "CSI overflow; sequence discarded");
            terminal_parser_reset();
        }
    }
}

static uint8_t target_rotation_for_a164(int present)
{
    if (!present)
        return s_base_rotation;
    return static_cast<uint8_t>(
        (s_base_rotation & ~3u) | ((s_base_rotation + 2u) & 3u));
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
        "RQ0 RW WORK COPY   VT100 LOCAL TERMINAL   A164 KEYBOARD",
        22, 50);

    s_term.x = 18;
    s_term.y = 84;
    s_term.w = W - 36;
    s_term.h = H - 102;
    s_term.text_size = 2.55f;

    M5.Display.drawRect(s_term.x - 2, s_term.y - 2,
                        s_term.w + 4, s_term.h + 4,
                        term_frame());
    M5.Display.fillRect(s_term.x, s_term.y,
                        s_term.w, s_term.h, term_bg());

    M5.Display.setTextSize(s_term.text_size);
    /* Remove only the extra 1px inter-character gap (not font size).
     * On the Tab5 screen this exposes 80 columns with the same glyphs. */
    s_term.cell_w = M5.Display.textWidth("M");
    s_term.line_h = M5.Display.fontHeight() + 3;
    if (s_term.cell_w < 8)
        s_term.cell_w = 8;
    if (s_term.line_h < 14)
        s_term.line_h = 14;

    s_term.cursor_x = s_term.x + 6;
    s_term.cursor_y = s_term.y + 6;
    s_saved_cursor_x = s_term.cursor_x;
    s_saved_cursor_y = s_term.cursor_y;

    M5.Display.setScrollRect(s_term.x,
                             s_term.y,
                             s_term.w,
                             s_term.h,
                             term_bg());
    M5.Display.setTextScroll(false);
    terminal_parser_reset();
    s_scroll_top=1;s_scroll_bottom=terminal_rows();
    s_cursor_enabled=true;s_dec_autowrap=true;
    s_cursor_painted=false;s_cursor_last_toggle=0;
    wifi_shadow_blank();

    ESP_LOGI("SIMHP4_RELEASE","VT100 cursor/scroll/erase and A164 DEL active");
    ESP_LOGI("SIMHP4_RC5_VI_FULLWINDOW","BSD init: stty 9600 and EXINIT window=25");

    ESP_LOGI(TAG,
             "TERMINAL SCREEN ready W=%d H=%d pane=%d,%d %dx%d cell=%dx%d rotation=%u",
             W, H,
             s_term.x, s_term.y, s_term.w, s_term.h,
             s_term.cell_w, s_term.line_h,
             (unsigned)M5.Display.getRotation());
}

extern "C" void simhp4_vax_terminal_redraw_after_wifi(void) {
    // Same presenter task, once per UI close. Do not modify guest/VT state.
    const float oldx=M5.Display.getTextSizeX(),oldy=M5.Display.getTextSizeY();
    M5.Display.startWrite();
    M5.Display.fillScreen(term_bg());
    M5.Display.setTextColor(term_text());M5.Display.setTextSize(2.4f);
    M5.Display.drawString("simhP4  MICROVAX II / KA630 CONSOLE",20,12);
    M5.Display.setTextColor(term_dim());M5.Display.setTextSize(1.45f);
    M5.Display.drawString("RQ0 RW WORK COPY   VT100 LOCAL TERMINAL   A164 KEYBOARD",22,50);
    M5.Display.drawRect(s_term.x-2,s_term.y-2,s_term.w+4,s_term.h+4,term_frame());
    M5.Display.setTextSize(s_term.text_size);M5.Display.setTextColor(term_text());
    const int nrow=terminal_rows()<50?terminal_rows():50;
    const int ncol=terminal_cols()<160?terminal_cols():160;
    for(int r=0;r<nrow;++r)for(int c=0;c<ncol;++c) {
        unsigned char v=(unsigned char)s_wifi_console_shadow[r][c];
        if(v>='!'&&v<='~')M5.Display.drawChar(v,s_term.x+6+c*s_term.cell_w,s_term.y+6+r*s_term.line_h);
    }
    M5.Display.setTextSize(oldx,oldy);
    M5.Display.endWrite();
    s_cursor_painted=false;s_cursor_last_toggle=0;
}

static void terminal_apply_orientation(bool force)
{
    const int present = simhp4_a164_keyboard_present();
    const uint32_t now=(uint32_t)(xTaskGetTickCount()*portTICK_PERIOD_MS);
    if(force){
        s_a164_candidate=present;s_a164_candidate_ms=now;
    }else{
        /* Release: transient I2C/A164 hotplug glitches must NOT blank LCD or
         * discard terminal shadow. Require 700ms stable before switching. */
        if(present!=s_a164_candidate){
            s_a164_candidate=present;s_a164_candidate_ms=now;return;
        }
        if(present==s_last_a164_present ||
           (uint32_t)(now-s_a164_candidate_ms)<700u)return;
    }
    ESP_LOGI(TAG,"SIMHP4_RC13_A164_STABLE present=%d",present);
    s_last_a164_present = present;
    const uint8_t target = target_rotation_for_a164(present);
    if (M5.Display.getRotation() != target)
        M5.Display.setRotation(target);

    ESP_LOGI(TAG,
             "ORIENTATION base=%u target=%u present=%d",
             (unsigned)s_base_rotation,
             (unsigned)target,
             present);
    terminal_draw_chrome();
}

static void terminal_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "TERMINAL TASK CPU%d prio=%u",
             xPortGetCoreID(),
             (unsigned)uxTaskPriorityGet(nullptr));

    /* RC14: the VAX main/SIMH task executes on CPU0.  Rendering plus the
     * overlay/touch service is deliberately assigned to CPU1 as ONE owner of
     * M5GFX/M5.update().  Continuous ASCII animations must still let IDLE1
     * run periodically (we do not disable or feed either WDT). */
    TickType_t last_busy_yield = xTaskGetTickCount();
    const TickType_t busy_yield_period = pdMS_TO_TICKS(250) > 0 ?
        pdMS_TO_TICKS(250) : (TickType_t)1;
    for (;;) {
        terminal_apply_orientation(false);

        uint8_t ch = 0;
        if (!simhp4_vax_terminal_dequeue_timed(&ch, 15)) {
            simhp4_wifi_overlay_pump();
            terminal_cursor_tick();
            continue;
        }

        terminal_cursor_erase();
        simhp4_wifi_boot_gate_feed(ch);
        terminal_putc(ch);

        for (unsigned i = 0; i < 127u; ++i) {
            if (!simhp4_vax_terminal_dequeue_timed(&ch, 0))
                break;
            simhp4_wifi_boot_gate_feed(ch);
                terminal_putc(ch);
        }
        simhp4_wifi_overlay_pump();
        terminal_cursor_tick();
        /* Only the busy, continuous-output path needs this blocked tick;
         * the no-input path above already blocks on FIFO receive (15 ms).
         * Maximum additional sleep: ~1 RTOS tick per 250ms of host time. */
        const TickType_t now = xTaskGetTickCount();
        if ((TickType_t)(now - last_busy_yield) >= busy_yield_period) {
            last_busy_yield = now;
            vTaskDelay(1);
        }
    }
}

extern "C" int simhp4_vax_terminal_start(void)
{
    if (!wifi_shadow_allocate()) return 0;
    if (!simhp4_vax_terminal_fifo_init()) {
        ESP_LOGE(TAG, "TTO FIFO allocation failed");
        return 0;
    }

    /* Capture native Tab5 orientation before A164 changes the local seat. */
    s_base_rotation = M5.Display.getRotation();
    s_last_a164_present = -1;
    terminal_apply_orientation(true);

    if (s_terminal_task != nullptr)
        return 1;

    const BaseType_t ok = xTaskCreatePinnedToCore(
        terminal_task,
        "simhp4_vax_term",
        8192,
        nullptr,
        1,
        &s_terminal_task,
        1);

    if (ok != pdPASS) {
        s_terminal_task = nullptr;
        ESP_LOGE(TAG, "RC14 CPU1 terminal presenter task create failed");
        return 0;
    }

    ESP_LOGI(TAG,
             "SIMHP4_RC14_TERMINAL_CPU1 READY fifo=2048 presenter=1 CPU1 prio=1 vt100_no_options=1 orientation180=%d drops=%lu",
             simhp4_a164_keyboard_present(),
             (unsigned long)simhp4_vax_terminal_drop_count());
    return 1;
}
