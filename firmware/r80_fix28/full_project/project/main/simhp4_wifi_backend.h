#pragma once
#include "simhp4_wifi_settings.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Immutable caller-owned snapshot. NEVER contains passwords. */
typedef struct {
    unsigned revision;
    int online;
    int driver_ready;
    int link_state;
    char ssid[SIMHP4_WIFI_SSID_SIZE];
    char ip[20];
    char error[48];
    simhp4_wifi_ap aps[SIMHP4_WIFI_MAX_APS];
    int ap_count;
} simhp4_wifi_snapshot_t;
int simhp4_wifi_backend_start(void);
int simhp4_wifi_backend_snapshot(simhp4_wifi_snapshot_t *dst);
void simhp4_wifi_backend_scan(void);
void simhp4_wifi_backend_join(const char *ssid,const char *password);
void simhp4_wifi_backend_leave(void);
void simhp4_wifi_backend_forget(void);
#ifdef __cplusplus
}
#endif
