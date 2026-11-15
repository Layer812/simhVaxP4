#ifndef SIMHP4_WIFI_SETTINGS_H
#define SIMHP4_WIFI_SETTINGS_H
#include <stddef.h>
#include <stdint.h>
#include "simhp4_wifi_badge.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SIMHP4_WIFI_MAX_APS 12
#define SIMHP4_WIFI_SSID_SIZE 33
#define SIMHP4_WIFI_PASS_SIZE 65

typedef struct {
 char ssid[SIMHP4_WIFI_SSID_SIZE];
 int rssi;
 int secure;
} simhp4_wifi_ap;
typedef struct {
 void *opaque;
 /* Request callbacks must enqueue work and return immediately; no Wi-Fi operations on presenter */
 void (*request_scan)(void *);
 void (*request_connect)(void *, const char *ssid, const char *password);
 void (*request_disconnect)(void *);
 void (*request_forget)(void *);
} simhp4_wifi_ui_ops;
typedef struct {
 simhp4_wifi_badge_theme colors;
 uint16_t text_secondary;
 uint16_t dialog_bg;
 int status_bar_y;
 int status_bar_height;
 int x_margin;
} simhp4_wifi_ui_theme;
typedef struct {
 int width, height;
 int open, keyboard, uppercase;
 int page, show_password;
 char radio_status[48];
 simhp4_wifi_link_state state;
 char connected_ssid[SIMHP4_WIFI_SSID_SIZE];
 char address[20];
 char ssid[SIMHP4_WIFI_SSID_SIZE];
 char password[SIMHP4_WIFI_PASS_SIZE];
 size_t password_len;
 simhp4_wifi_ap aps[SIMHP4_WIFI_MAX_APS];
 int ap_count;
 int selected;
 int revision;
 simhp4_wifi_ui_ops ops;
} simhp4_wifi_ui;

void simhp4_wifi_ui_init(simhp4_wifi_ui *, int w, int h, simhp4_wifi_ui_ops);
void simhp4_wifi_ui_set_link(simhp4_wifi_ui *, simhp4_wifi_link_state, const char *ssid, const char *ip);
void simhp4_wifi_ui_set_radio_status(simhp4_wifi_ui *, const char *status);
void simhp4_wifi_ui_set_aps(simhp4_wifi_ui *, const simhp4_wifi_ap *, int count);
/* Return 1 to consume host touch; return 0 to forward to guest. */
int simhp4_wifi_ui_touch(simhp4_wifi_ui *, int x, int y);
void simhp4_wifi_ui_draw(simhp4_wifi_ui *, const simhp4_wifi_ui_theme *, const simhp4_wifi_badge_painter *);
int simhp4_wifi_ui_is_open(const simhp4_wifi_ui *);
#ifdef __cplusplus
}
#endif
#endif
