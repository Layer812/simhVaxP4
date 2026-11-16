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
#include "esp_heap_caps.h"
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
/* Render modal pages offscreen in PSRAM, then present a completed frame.
 * The old direct path cleared the live LCD on every key/touch/release,
 * making white/black flashes unavoidable even without a host WDT reset.
 * One canvas only: ~1.76 MiB at 1280x720 RGB565, no extra presenter task. */
static M5Canvas modal_canvas(&M5.Display);
static bool modal_canvas_ready=false;
static bool modal_canvas_attempted=false;
static bool in_canvas=false;
/* FIX22: Keep small tile signatures and the network snapshot in PSRAM.
 * One 1280x720 RGB565 sprite is retained from FIX21; NO second framebuffer,
 * NO additional task, NO extra internal DMA buffer or stack allocation.
 * Presenter task remains 8192 bytes (no internal heap increase). */
static constexpr int kTileW=80, kTileH=60;
static uint32_t *tile_signatures=nullptr;
static int tile_cols=0,tile_rows=0;
static bool signature_valid=false,canvas_visible=false;
static uint8_t last_rotation=255;
static simhp4_wifi_snapshot_t *snapshot_psram=nullptr;
static bool snapshot_tried=false;
static bool tile_tried=false;
static bool scan_requested=false;
static uint32_t last_stack_log_ms=0;
static bool low_stack_warned=false;
static uint32_t hash_tile(const uint8_t *frame,int tx,int ty,int width,int height){
    const int x0=tx*kTileW,y0=ty*kTileH;
    const int x1=(x0+kTileW<width)?x0+kTileW:width;
    const int y1=(y0+kTileH<height)?y0+kTileH:height;
    uint32_t h=2166136261u;
    for(int y=y0;y<y1;++y){
        const uint8_t *p=frame+(size_t)y*(size_t)width*2u+(size_t)x0*2u;
        for(int k=0;k<(x1-x0)*2;++k){h=(h^p[k])*16777619u;}
    }
    return h;
}
static void prepare_psram_scratch(){
    if(!snapshot_tried){
        snapshot_tried=true;
        snapshot_psram=static_cast<simhp4_wifi_snapshot_t *>(
            heap_caps_calloc(1,sizeof(simhp4_wifi_snapshot_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if(!snapshot_psram)ESP_LOGE(TAG,"FIX22 snapshot PSRAM allocation failed; no stack fallback");
    }
    if(!tile_tried){
        tile_tried=true;
        const int w=M5.Display.width(),h=M5.Display.height();
        tile_cols=(w+kTileW-1)/kTileW;
        tile_rows=(h+kTileH-1)/kTileH;
        const size_t n=(size_t)tile_cols*(size_t)tile_rows;
        if(n && n<=1024u)
            tile_signatures=static_cast<uint32_t *>(heap_caps_calloc(n,sizeof(uint32_t),
                MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if(!tile_signatures)ESP_LOGW(TAG,"FIX22 partial refresh unavailable; full-frame fallback");
    }
}
static void present_canvas(){
    const int w=M5.Display.width(),h=M5.Display.height();
    const uint8_t *pixels=static_cast<const uint8_t *>(modal_canvas.getBuffer());
    if(!tile_signatures||!pixels||w!=modal_canvas.width()||h!=modal_canvas.height()){
        modal_canvas.pushSprite(0,0);return;
    }
    const bool full=!signature_valid || !canvas_visible;
    int left=w,top=h,right=0,bottom=0;
    for(int ty=0;ty<tile_rows;++ty){
        for(int tx=0;tx<tile_cols;++tx){
            const size_t i=(size_t)ty*(size_t)tile_cols+(size_t)tx;
            uint32_t digest=hash_tile(pixels,tx,ty,w,h);
            if(full||tile_signatures[i]!=digest){
                const int x=tx*kTileW,y=ty*kTileH;
                if(x<left)left=x;
                if(y<top)top=y;
                if(x+kTileW>right)right=x+kTileW;
                if(y+kTileH>bottom)bottom=y+kTileH;
            }
            tile_signatures[i]=digest;
        }
    }
    signature_valid=true;
    if(right>left&&bottom>top){
        if(right>w)right=w;
        if(bottom>h)bottom=h;
        if(!full && (right-left)*(bottom-top)<w*h){
            M5.Display.setClipRect(left,top,right-left,bottom-top);
            modal_canvas.pushSprite(0,0);
            M5.Display.clearClipRect();
        }else modal_canvas.pushSprite(0,0);
    }
    canvas_visible=true;
}
static void record_stack_budget(uint32_t now){
    /* ESP-IDF on ESP32-P4 returns stack high-water mark in BYTES. */
#if (INCLUDE_uxTaskGetStackHighWaterMark == 1)
    const uint32_t free_bytes=(uint32_t)uxTaskGetStackHighWaterMark(nullptr);
    const bool low=free_bytes<2048u;
    if((uint32_t)(now-last_stack_log_ms)>=10000u || !last_stack_log_ms || (low&&!low_stack_warned)){
        last_stack_log_ms=now?now:1u;
        ESP_LOGI(TAG,"FIX22_PRESENTER_STACK min_free_bytes=%lu stack_alloc=8192 dma_internal_free=%lu psram_free=%lu",
                 (unsigned long)free_bytes,
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA),
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        if(low){
            ESP_LOGW(TAG,"FIX22_PRESENTER_STACK_LOW min_free_bytes=%lu",(unsigned long)free_bytes);
            low_stack_warned=true;
        }
    }
#else
    (void)now;
#endif
}
static void fill(void*,int x,int y,int w,int h,uint16_t c){
    if(in_canvas)modal_canvas.fillRect(x,y,w,h,c);
    else M5.Display.fillRect(x,y,w,h,c);
}
static void line(void*,int a,int b,int c,int d,uint16_t color){
    if(in_canvas)modal_canvas.drawLine(a,b,c,d,color);
    else M5.Display.drawLine(a,b,c,d,color);
}
static void draw_text(void*,const char*s,int x,int y,uint16_t c){
    const size_t n=std::strlen(s);
    const float size=(std::strcmp(s,"DISCONNECT")==0?2.05f:(n>=26?2.25f:(n>=10?2.35f:2.85f)));
    if(in_canvas){
        modal_canvas.setTextSize(size);
        modal_canvas.setTextColor(c);
        modal_canvas.drawString(s,x,y);
    }else{
        M5.Display.setTextSize(size);
        M5.Display.setTextColor(c);
        M5.Display.drawString(s,x,y);
    }
}
static void prepare_canvas(){
    if(modal_canvas_attempted)return;
    modal_canvas_attempted=true;
    modal_canvas.setPsram(true);
    modal_canvas.setColorDepth(16);
    modal_canvas_ready=modal_canvas.createSprite(M5.Display.width(),M5.Display.height())!=nullptr;
    ESP_LOGI(TAG,"RC13 UI framebuffer PSRAM=%s size=%dx%d",
             modal_canvas_ready?"READY":"UNAVAILABLE (direct fallback)",
             M5.Display.width(),M5.Display.height());
}
static bool backend_started=false;
static void scan(void*){
  // Start AFTER the first modal frame is visible. No extra worker/stack.
  // Only an explicit human Wi-Fi tap or RE-SCAN sets this flag.
  scan_requested=true;
}
static void connect(void*,const char *ssid,const char *password){simhp4_wifi_backend_join(ssid,password);}
static void disconnect(void*){simhp4_wifi_backend_leave();}
static void forget(void*){simhp4_wifi_backend_forget();}
static void paint(){
    simhp4_wifi_badge_painter p={};p.fill_rect=&fill;p.draw_line=&line;p.draw_text=&draw_text;
    if(ui.open){prepare_psram_scratch();prepare_canvas();}
    in_canvas=ui.open && modal_canvas_ready;
    const float old_size_x=M5.Display.getTextSizeX();
    const float old_size_y=M5.Display.getTextSizeY();
    if(in_canvas){
        modal_canvas.startWrite();
        simhp4_wifi_ui_draw(&ui,&theme,&p);
        modal_canvas.endWrite();
        // Tile signatures are PSRAM-resident; clip the completed sprite to
        // changed content. First open always transfers a complete frame.
        present_canvas();
    }else{
        canvas_visible=false;
        signature_valid=false;
        M5.Display.startWrite();
        simhp4_wifi_ui_draw(&ui,&theme,&p);
        M5.Display.endWrite();
    }
    in_canvas=false;
    // Keep the established >=20px console font after leaving UI.
    M5.Display.setTextSize(old_size_x,old_size_y);
}
static uint32_t last_pump_ms=0;
static uint32_t last_backend_poll_ms=0;
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
    ESP_LOGI(TAG,"SIMHP4_RC6_TOUCH_LIVE sample=30ms edge+begin");
}
 } // namespace
extern "C" void simhp4_wifi_overlay_capture_touch(void) {
    // app_main task, immediately after the EXISTING M5.update().
    // No extra task, no LCD drawing, no wireless service startup.
    if (!simhp4_wifi_boot_gate_ready()) return;
    // RC6: M5Unified provides a per-update begin edge.  Do not rely solely on
    // a sampled isPressed() rising edge: a missed release can latch `prior`
    // and make subsequent taps appear dead after the device has been running.
    // The presenter is the sole M5.update() and touch-state owner.
    static bool prior=false;
    const auto detail=M5.Touch.getDetail(0);
    const bool pressed=(M5.Touch.getCount()!=0u) && detail.isPressed();
    const bool began=detail.wasPressed();
    if (began || (pressed && !prior)) {
        const uint32_t x=(uint32_t)detail.x;
        const uint32_t y=(uint32_t)detail.y;
        if (x < (uint32_t)M5.Display.width() &&
            y < (uint32_t)M5.Display.height() &&
            x<=2047u && y<=2047u) {
            pending_touch.store(0x80000000u | (y<<11) | x,
                                std::memory_order_release);
            // Log accepted physical events, not continuous polling.
            ESP_LOGI(TAG,"RC6 TOUCH event x=%lu y=%lu begin=%d",
                     (unsigned long)x,(unsigned long)y,(int)began);
        }
    }
    prior=pressed;
}
extern "C" void simhp4_wifi_overlay_pump(void) {
    // Called ONLY from the EXISTING terminal presenter task. No UI task.
    // Do not allocate a second M5.update or LCD-writer worker pre-boot.
    if (!simhp4_wifi_boot_gate_ready()) return;
    if (!ui_ready) init_after_shell();
    uint32_t now=(uint32_t)(xTaskGetTickCount()*portTICK_PERIOD_MS);
    // A164 hotplug can redraw the LCD underneath the modal.
    const uint8_t rotation=M5.Display.getRotation();
    if(last_rotation!=rotation){
        last_rotation=rotation;canvas_visible=false;signature_valid=false;
        displayed_revision=-1;
    }
    // RC6: 100-ms polling loses quick taps and may miss the release edge.
    // Touch is sampled at 30 ms; network snapshots stay at 100 ms to avoid
    // doing additional Wi-Fi/mutex work on every presenter pass.
    if ((uint32_t)(now-last_pump_ms)<30u) return;
    last_pump_ms=now;
    // R6R44: SIMH blocks app_main. Presenter is the sole M5.update owner.
    M5.update();
    simhp4_wifi_overlay_capture_touch();
    const uint32_t event=pending_touch.exchange(0,std::memory_order_acq_rel);
    if ((event & 0x80000000u)!=0u) {
        const int x=(int)(event & 0x7ffu);
        const int y=(int)((event >> 11) & 0x7ffu);
        simhp4_wifi_ui_touch(&ui,x,y);
    }
    if (ui.open && !snapshot_tried) prepare_psram_scratch();
    if ((uint32_t)(now-last_backend_poll_ms)>=100u) {
        last_backend_poll_ms=now;
        /* The Wi-Fi snapshot used to be a large automatic variable in the
         * CPU1 presenter task's 8192-byte stack. It now lives in PSRAM. */
        if (snapshot_psram && simhp4_wifi_backend_snapshot(snapshot_psram) &&
            snapshot_psram->revision!=backend_revision) {
            backend_revision=snapshot_psram->revision;
            simhp4_wifi_ui_set_link(&ui,(simhp4_wifi_link_state)snapshot_psram->link_state,
                                    snapshot_psram->ssid,snapshot_psram->ip);
            simhp4_wifi_ui_set_aps(&ui,snapshot_psram->aps,snapshot_psram->ap_count);
            simhp4_wifi_ui_set_radio_status(&ui,snapshot_psram->error[0]
                                             ?snapshot_psram->error
                                             :(snapshot_psram->driver_ready?"READY":"STARTING"));
        }
    }
    /* Presenter clock also drives pressed-state release even with no network events. */
    simhp4_wifi_ui_progress(&ui,now,backend_revision==UINT32_MAX?0:backend_revision);
    if(ui.revision!=displayed_revision) {
        displayed_revision=ui.revision;
        static bool was_modal=false;
        if(was_modal && !ui.open)simhp4_vax_terminal_redraw_after_wifi();
        was_modal=ui.open;
        paint();
    }
    if(ui.open){
        record_stack_budget(now);
        // This synchronous C6/SDIO startup used to block BEFORE the first
        // Wi-Fi page was painted. Showing the page first avoids a blank wait.
        if(scan_requested){
            scan_requested=false;
            if(!backend_started)backend_started=simhp4_wifi_backend_start()!=0;
            if(backend_started)simhp4_wifi_backend_scan();
        }
    }else{
        scan_requested=false;
    }
}

extern "C" int simhp4_wifi_overlay_is_open(void) { return ui.open; }
