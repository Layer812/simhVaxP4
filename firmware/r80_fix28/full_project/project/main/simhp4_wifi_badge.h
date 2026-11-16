/* simhP4 Wi-Fi status badge -- theme-owned rendering primitive.
 * The caller supplies its EXISTING UI palette, typeface and framebuffer owner.
 * This module does not initialize Wi-Fi, touch hardware, M5GFX, or modify the guest.
 * C99 / C++17 compatible. */
#ifndef SIMHP4_WIFI_BADGE_H
#define SIMHP4_WIFI_BADGE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum simhp4_wifi_link_state {
    SIMHP4_WIFI_UNCONFIGURED = 0,
    SIMHP4_WIFI_DISCONNECTED = 1,
    SIMHP4_WIFI_CONNECTING = 2,
    SIMHP4_WIFI_IP_READY = 3
} simhp4_wifi_link_state;

typedef struct simhp4_wifi_badge_rect { int x, y, width, height; } simhp4_wifi_badge_rect;
typedef struct simhp4_wifi_badge_theme {
    uint16_t accent_green;     /* supplied from the existing MicroVAX UI palette */
    uint16_t white;            /* text on the connected green background */
    uint16_t black;            /* exact existing MicroVAX near-black background */
    uint16_t green_fill;       /* existing medium green; not a newly invented color */
    int border_width;          /* same line weight as other status controls */
} simhp4_wifi_badge_theme;

typedef struct simhp4_wifi_badge_painter {
    void *context; /* host presenter / owning UI task -- NEVER Wi-Fi event callback */
    void (*fill_rect)(void *, int x, int y, int w, int h, uint16_t rgb565);
    void (*draw_line)(void *, int x0, int y0, int x1, int y1, uint16_t rgb565);
    /* draw_text MUST preserve the existing UI font and glyph scale */
    void (*draw_text)(void *, const char *utf8, int x, int y, uint16_t rgb565);
} simhp4_wifi_badge_painter;

/* State is drawn as CONNECTED only after both association + DHCP/IP ready. */
int simhp4_wifi_badge_connected(simhp4_wifi_link_state state);
/* Coordinate hit testing belongs to host UI; consume before forwarding to guest. */
int simhp4_wifi_badge_hit(const simhp4_wifi_badge_rect *rect, int x, int y);
/* Draw only on UI/presenter task inside existing screen ownership protocol. */
int simhp4_wifi_badge_draw(const simhp4_wifi_badge_rect *rect,
                           const simhp4_wifi_badge_theme *theme,
                           const simhp4_wifi_badge_painter *painter,
                           simhp4_wifi_link_state state);
#ifdef __cplusplus
}
#endif
#endif
