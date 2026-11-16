/* R6R41: existing terminal presenter owns touch and redraw on changes only.
 * Network callback never paints, expensive recurring 150ms modal redraw REMOVED.
 */
#include "simhp4_wifi_overlay.h"
#include "simh_wifi_boot_gate.h"
#include "simhp4_wifi_settings.h"
#include "simhp4_wifi_backend.h"
extern "C" void simhp4_vax_terminal_redraw_after_wifi(void);
#include <M5Unified.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdint>
#include <atomic>
#include <cstring>
#include <cstdio>
namespace {
static const char *TAG="SIMHP4_WIFI_UI";
static simhp4_wifi_ui ui={};
static simhp4_wifi_ui_theme theme={};
static void fill(void*,int x,int y,int w,int h,uint16_t c){M5.Display.fillRect(x,y,w,h,c);}
static void line(void*,int a,int b,int c,int d,uint16_t color){M5.Display.drawLine(a,b,c,d,color);}
static void draw_text(void*,const char*s,int x,int y,uint16_t c){
  // 1280x720 physical Tab5 needs >=20px text, not the 8px M5GFX default.
  // Long control labels are kept within their exact touch target width.
  const size_t n=std::strlen(s);
  const float size=(std::strcmp(s,"DISCONNECT")==0?2.05f:(n>=26?2.25f:(n>=10?2.35f:2.85f)));
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(c);
  M5.Display.drawString(s,x,y);
}
static bool backend_started=false;
static void scan(void*){
  // Radio/SDIO deliberately uninitialized throughout golden R6R30 boot.
  // First explicit human tap starts backend worker and requests AP scan.
  if(!backend_started) backend_started=simhp4_wifi_backend_start()!=0;
  if(backend_started) simhp4_wifi_backend_scan();
}
static void connect(void*,const char *ssid,const char *password){simhp4_wifi_backend_join(ssid,password);}
static void disconnect(void*){simhp4_wifi_backend_leave();}
static void forget(void*){simhp4_wifi_backend_forget();}
static void paint(){
    simhp4_wifi_badge_painter p={};p.fill_rect=&fill;p.draw_line=&line;p.draw_text=&draw_text;
    // Paint only on actual UI state changes. Terminal task remains the ONLY presenter.
    M5.Display.startWrite();
    const float old_size_x=M5.Display.getTextSizeX();
    const float old_size_y=M5.Display.getTextSizeY();
    simhp4_wifi_ui_draw(&ui,&theme,&p);
    // The fullscreen UI draws C6 status itself, within the right panel.
    // Do not reset to 1.0 (tiny) and accidentally shrink the guest terminal.
    M5.Display.setTextSize(old_size_x,old_size_y);
    M5.Display.endWrite();
}
static uint32_t last_pump_ms=0;
static bool ui_ready=false;
static std::atomic<uint32_t> pending_touch{0};
static int displayed_revision=-1;
static unsigned backend_revision=UINT32_MAX;
static void init_after_shell(void) {
    simhp4_wifi_ui_ops cb={};cb.request_scan=&scan;cb.request_connect=&connect;
    cb.request_disconnect=&disconnect;cb.request_forget=&forget;
    simhp4_wifi_ui_init(&ui,M5.Display.width(),M5.Display.height(),cb);
    theme.colors.accent_green=M5.Display.color565(255,255,255);
    theme.colors.green_fill=M5.Display.color565(255,255,255);
    theme.colors.black=M5.Display.color565(0,0,0);
    theme.colors.white=M5.Display.color565(255,255,255);
    theme.colors.border_width=2;
    theme.text_secondary=M5.Display.color565(189,189,189);
    theme.dialog_bg=M5.Display.color565(0,0,0);
    theme.status_bar_y=0;theme.status_bar_height=84;theme.x_margin=18;
    simhp4_wifi_ui_set_link(&ui,SIMHP4_WIFI_DISCONNECTED,"","");
    ui_ready=true;
    ESP_LOGI(TAG,"R6R41 guest prompt gate OPEN; Hosted OFF until Wi-Fi tap");
}
 } // namespace
extern "C" void simhp4_wifi_overlay_capture_touch(void) {
    // app_main task, immediately after the EXISTING M5.update().
    // No extra task, no LCD drawing, no wireless service startup.
    if (!simhp4_wifi_boot_gate_ready()) return;
    static bool prior=false; // only the app_main task touches this
    auto detail=M5.Touch.getDetail(0);
    const bool pressed=detail.isPressed();
    if (pressed && !prior) {
        const uint32_t x=(uint32_t)detail.x;
        const uint32_t y=(uint32_t)detail.y;
        if (x<=2047u && y<=2047u)
            pending_touch.store(0x80000000u | (y<<11) | x,
                                std::memory_order_release);
    }
    prior=pressed;
}
extern "C" void simhp4_wifi_overlay_pump(void) {
    // Called ONLY from the EXISTING terminal presenter task. No UI task.
    // Do not allocate a second M5.update or LCD-writer worker pre-boot.
    if (!simhp4_wifi_boot_gate_ready()) return;
    if (!ui_ready) init_after_shell();
    uint32_t now=(uint32_t)(xTaskGetTickCount()*portTICK_PERIOD_MS);
    if ((uint32_t)(now-last_pump_ms)<100u) return;
    last_pump_ms=now;
    // R6R44: SIMH blocks app_main. Presenter is the active touch owner.
    // Call update exactly once per 100 ms, after the prompt readiness gate.
    M5.update();
    simhp4_wifi_overlay_capture_touch();
    const uint32_t event=pending_touch.exchange(0,std::memory_order_acq_rel);
    if ((event & 0x80000000u)!=0u) {
        const int x=(int)(event & 0x7ffu);
        const int y=(int)((event >> 11) & 0x7ffu);
        simhp4_wifi_ui_touch(&ui,x,y);
    }
    simhp4_wifi_snapshot_t s={};
    if (simhp4_wifi_backend_snapshot(&s) && s.revision!=backend_revision) {
        backend_revision=s.revision;
        simhp4_wifi_ui_set_link(&ui,(simhp4_wifi_link_state)s.link_state,s.ssid,s.ip);
        simhp4_wifi_ui_set_aps(&ui,s.aps,s.ap_count);
        simhp4_wifi_ui_set_radio_status(&ui,s.error[0]?s.error:(s.driver_ready?"READY":"STARTING"));
    }
    if(ui.revision!=displayed_revision) {
        displayed_revision=ui.revision;
        static bool was_modal=false;
        if(was_modal && !ui.open)simhp4_vax_terminal_redraw_after_wifi();
        was_modal=ui.open;
        paint();
    }
}

extern "C" int simhp4_wifi_overlay_is_open(void) { return ui.open; }
