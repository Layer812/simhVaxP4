#include "simhp4_wifi_badge.h"

int simhp4_wifi_badge_connected(simhp4_wifi_link_state state)
{
    return state == SIMHP4_WIFI_IP_READY;
}

int simhp4_wifi_badge_hit(const simhp4_wifi_badge_rect *r, int x, int y)
{
    if (!r || r->width <= 0 || r->height <= 0) return 0;
    return x >= r->x && y >= r->y &&
           (int64_t)x < (int64_t)r->x + r->width &&
           (int64_t)y < (int64_t)r->y + r->height;
}

static void hline(const simhp4_wifi_badge_painter *p, int x1, int y, int x2, uint16_t c)
{
    p->draw_line(p->context, x1, y, x2, y, c);
}
static void dot(const simhp4_wifi_badge_painter *p, int x, int y, uint16_t c)
{
    p->fill_rect(p->context, x, y, 3, 3, c);
}

int simhp4_wifi_badge_draw(const simhp4_wifi_badge_rect *r,
                           const simhp4_wifi_badge_theme *t,
                           const simhp4_wifi_badge_painter *p,
                           simhp4_wifi_link_state state)
{
    if (!r || !t || !p || !p->fill_rect || !p->draw_line || !p->draw_text ||
        r->width < 92 || r->height < 40 || t->border_width < 1 ||
        t->border_width > 4) return 0;

    const int online = simhp4_wifi_badge_connected(state);
    const uint16_t bg = online ? t->white : t->black;
    const uint16_t fg = online ? t->black : t->white;
    const int x = r->x, y = r->y, w = r->width, h = r->height;

    p->fill_rect(p->context, x, y, w, h, bg);
    /* Rectilinear border uses same stroke width as host UI; no new corners/shadows */
    for (int k = 0; k < t->border_width; ++k) {
        p->draw_line(p->context, x+k, y+k, x+w-1-k, y+k, t->white);
        p->draw_line(p->context, x+k, y+h-1-k, x+w-1-k, y+h-1-k, t->white);
        p->draw_line(p->context, x+k, y+k, x+k, y+h-1-k, t->white);
        p->draw_line(p->context, x+w-1-k, y+k, x+w-1-k, y+h-1-k, t->white);
    }
    /* Pixel/straight-line icon.  Font remains the original host font. */
    const int gx=x+13, gy=y+(h-25)/2;
    hline(p,gx+1,gy+5,gx+5,fg); hline(p,gx+6,gy+3,gx+19,fg);
    hline(p,gx+20,gy+5,gx+24,fg);
    hline(p,gx+4,gy+11,gx+8,fg); hline(p,gx+9,gy+9,gx+16,fg);
    hline(p,gx+17,gy+11,gx+21,fg);
    hline(p,gx+10,gy+17,gx+15,fg); dot(p,gx+12,gy+21,fg);
    p->draw_text(p->context, "Wi-Fi", x+50, y+(h-23)/2, fg);
    return 1;
}
