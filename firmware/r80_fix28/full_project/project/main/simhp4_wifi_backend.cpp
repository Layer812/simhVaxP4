/* R6R35 FIX3: Tab5 P4 -> ESP-Hosted (SDIO2) -> C6; host STA only.
 * No call to SIMH guest CPU, irq, disk, framebuffer, or NVS from the event callback.
 * P4 networking is REAL; guest qe0 / XQ / NAT is intentionally not claimed.
 */
#include "simhp4_wifi_backend.h"
extern "C" int simhp4_r79_run_auto_init_on_wifi_worker(void);
#include <M5Unified.h>
#include "esp_log.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_hosted_event.h"
#include "r71_guestnet.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h" // IDF 5.5: esp_netif_create_default_wifi_sta declaration
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstring>

// Official ESP-Hosted v3 event bridge; event callback is non-blocking.
static volatile uint32_t s_v3_transport_fault = 0;
static bool s_v3_transport_was_up = false;
extern "C" uint32_t simhp4_sdio_fault_code(void) {
    return __atomic_load_n(&s_v3_transport_fault,__ATOMIC_ACQUIRE);
}
static void hosted_v3_event_cb(void*,esp_event_base_t base,int32_t id,void*) {
    if (base!=ESP_HOSTED_EVENT) return;
    // Startup DOWN before the first confirmed UP is not a link failure.
    if (id==ESP_HOSTED_EVENT_TRANSPORT_UP) {
        __atomic_store_n(&s_v3_transport_was_up,true,__ATOMIC_RELEASE);
        return;
    }
    const bool was_up=__atomic_load_n(&s_v3_transport_was_up,__ATOMIC_ACQUIRE);
    const uint32_t fault = id==ESP_HOSTED_EVENT_TRANSPORT_FAILURE ? 21u :
        (id==ESP_HOSTED_EVENT_TRANSPORT_DOWN && was_up) ? 22u : 0u;
    if (id==ESP_HOSTED_EVENT_TRANSPORT_DOWN)
        __atomic_store_n(&s_v3_transport_was_up,false,__ATOMIC_RELEASE);
    if (fault) {
        uint32_t none=0u;
        (void)__atomic_compare_exchange_n(&s_v3_transport_fault,&none,fault,
                                           false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
    }
}
/* SIMHP4_R80_REV10_POWER: startup-only CPU yield coordination.
 * Called only by CPU host yield and the EXISTING Wi-Fi worker.
 * 60 seconds is a strict automatic upper bound; no new task/timer/ISR.
 */
#include "esp_timer.h"
static volatile uint32_t s_r80_wifi_power_phase=0;
static volatile uint32_t s_r80_wifi_phase_started_ms=0;
static void r80_wifi_power_begin(void) {
    __atomic_store_n(&s_r80_wifi_phase_started_ms,(uint32_t)(esp_timer_get_time()/1000LL),__ATOMIC_RELEASE);
    __atomic_store_n(&s_r80_wifi_power_phase,1u,__ATOMIC_RELEASE);
    ESP_LOGI("SIMHP4_POWER","WiFi critical startup phase BEGIN (max 60s)");
}
static void r80_wifi_power_end(void) {
    if (__atomic_exchange_n(&s_r80_wifi_power_phase,0u,__ATOMIC_ACQ_REL))
        ESP_LOGI("SIMHP4_POWER","WiFi critical startup phase END");
}
extern "C" int simhp4_r80_wifi_power_phase(void) {
    if (!__atomic_load_n(&s_r80_wifi_power_phase,__ATOMIC_ACQUIRE)) return 0;
    const uint32_t then=__atomic_load_n(&s_r80_wifi_phase_started_ms,__ATOMIC_ACQUIRE);
    const uint32_t now=(uint32_t)(esp_timer_get_time()/1000LL);
    if ((uint32_t)(now-then) >= 60000u) {
        __atomic_store_n(&s_r80_wifi_power_phase,0u,__ATOMIC_RELEASE);
        return 0;
    }
    return 1;
}

namespace {
constexpr const char* LOG = "SIMHP4_WIFI_STA";
enum class Action : uint8_t { Scan, Join, Leave, Forget };
struct Request { Action type; char ssid[33]; char password[65]; };
// Wi-Fi event-loop callback owns only an event queue; worker owns reconnect state.
static QueueHandle_t q = nullptr;
struct WifiNotice {
    uint8_t kind; // 1=disconnect, 2=got IP, 3=connected, 4=lost IP
    int reason;
    char ip[20];
    char ssid[33];
};
static QueueHandle_t notice_q = nullptr;
// IP notices have no SSID; only use events from the station netif.
static esp_netif_t* sta_netif = nullptr;
static uint32_t notice_overflow = 0;
static SemaphoreHandle_t lock = nullptr;
static simhp4_wifi_snapshot_t snapshot = {};
/* SIMHP4_R80_FIX11: identity only, never NVS password data. */
static char s_saved_identity[33] = {};
static void set_saved_identity(const char *ssid) {
    if (!lock) return;
    xSemaphoreTake(lock,portMAX_DELAY);
    std::snprintf(s_saved_identity,sizeof s_saved_identity,"%.32s",ssid?ssid:"");
    ++snapshot.revision;
    xSemaphoreGive(lock);
}
/* SIMHP4_R79_AP_AUTH_FLOW: retain actual C6 scan security through Join.
 * Only accessed on WiFi worker, never on guest CPU or event callback.
 */
struct ScannedAP {
    char ssid[33];
    int authmode;
    int channel;
    int rssi;
};
static ScannedAP scanned_aps[SIMHP4_WIFI_MAX_APS] = {};
static int scanned_count = 0;
static void retain_scanned_security(const wifi_ap_record_t *aps, int count) {
    scanned_count = 0;
    if (!aps || count<0 || count>SIMHP4_WIFI_MAX_APS) return;
    std::memset(scanned_aps,0,sizeof scanned_aps);
    for (int i=0;i<count;i++) {
        std::snprintf(scanned_aps[i].ssid,sizeof scanned_aps[i].ssid,
                      "%.32s",reinterpret_cast<const char*>(aps[i].ssid));
        scanned_aps[i].authmode = static_cast<int>(aps[i].authmode);
        scanned_aps[i].channel = static_cast<int>(aps[i].primary);
        scanned_aps[i].rssi = static_cast<int>(aps[i].rssi);
    }
    scanned_count = count;
}
static bool scanned_security_for(const char *ssid, ScannedAP *result) {
    if (!ssid || !result) return false;
    int best=-1;
    for (int i=0;i<scanned_count;i++) {
        if (std::strcmp(scanned_aps[i].ssid,ssid)==0 &&
            (best<0 || scanned_aps[i].rssi > scanned_aps[best].rssi))
            best=i;
    }
    if (best<0) return false;
    *result=scanned_aps[best];
    return true;
}
static char pending_ssid[33] = {};
static char pending_password[65] = {};
static bool pending_save = false;
static bool reconnect = false;
static bool ready = false;
static TickType_t next_retry = 0;
// Only the Wi-Fi worker changes these fields. They are never touched by event_cb.
static bool sta_connected_notice = false;
static bool switching_network = false;
static Request staged_join = {};
static bool staged_join_save = false;
static char disconnect_from_ssid[33] = {};
static TickType_t disconnect_deadline = 0;
static void update_state(const char *reason,int link) {
    if(link!=SIMHP4_WIFI_IP_READY) r71_guestnet_link(false);
    if (!lock) return;
    xSemaphoreTake(lock,portMAX_DELAY);
    snapshot.link_state=link;
    snapshot.online = link==SIMHP4_WIFI_IP_READY;
    if (reason) std::snprintf(snapshot.error,sizeof(snapshot.error),"%s",reason);
    if (link!=SIMHP4_WIFI_IP_READY) snapshot.ip[0]=0;
    ++snapshot.revision;
    xSemaphoreGive(lock);
}
static void update_message(const char *message) {
    if (!lock || !message) return;
    xSemaphoreTake(lock,portMAX_DELAY);
    std::snprintf(snapshot.error,sizeof(snapshot.error),"%s",message);
    ++snapshot.revision;
    xSemaphoreGive(lock);
}
static void save_network() {
    if (!pending_save || !pending_ssid[0]) return;
    nvs_handle_t h;
    if (nvs_open("simhp4wifi",NVS_READWRITE,&h)==ESP_OK) {
        if (nvs_set_str(h,"ssid",pending_ssid)==ESP_OK &&
            nvs_set_str(h,"pass",pending_password)==ESP_OK) {
            esp_err_t r=nvs_commit(h);
            if(r!=ESP_OK) ESP_LOGW(LOG,"Credential commit failed: %s",esp_err_to_name(r));
            else set_saved_identity(pending_ssid);
        }
        nvs_close(h);
    }
    std::memset(pending_password,0,sizeof pending_password);
    pending_save=false;
}
// SIMHP4_R6R65_STA_TRANSITION: events carry the AP identity when supplied by
// ESP-IDF; only the worker can change credentials, reconnect, or UI state.
static void event_cb(void*,esp_event_base_t base,int32_t id,void *data) {
    if (simhp4_sdio_fault_code()) return;
    WifiNotice n = {};
    if (base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        n.kind=1;
        const auto *ev=static_cast<const wifi_event_sta_disconnected_t*>(data);
        n.reason=ev?static_cast<int>(ev->reason):-1;
        if (ev) {
            size_t len=ev->ssid_len;
            if (len>32) len=32;
            std::memcpy(n.ssid,ev->ssid,len);
        }
    } else if (base==WIFI_EVENT && id==WIFI_EVENT_STA_CONNECTED) {
        n.kind=3;
        const auto *ev=static_cast<const wifi_event_sta_connected_t*>(data);
        if (ev) {
            size_t len=ev->ssid_len;
            if (len>32) len=32;
            std::memcpy(n.ssid,ev->ssid,len);
        }
    } else if (base==IP_EVENT && id==IP_EVENT_STA_GOT_IP && data) {
        n.kind=2;
        const auto *ip=static_cast<const ip_event_got_ip_t*>(data);
        if (!sta_netif || ip->esp_netif != sta_netif) return;
        std::snprintf(n.ip,sizeof n.ip,IPSTR,IP2STR(&ip->ip_info.ip));
    } else if (base==IP_EVENT && id==IP_EVENT_STA_LOST_IP) {
        n.kind=4; // no SSID is supplied by this event
    } else return;
    if (!notice_q || xQueueSend(notice_q,&n,0)!=pdTRUE) {
        __atomic_add_fetch(&notice_overflow,1u,__ATOMIC_RELAXED);
    }
}
static void begin_join_now(const char *ssid,const char *password,bool save);
static void discard_notices() {
    WifiNotice n = {};
    if (notice_q) while (xQueueReceive(notice_q,&n,0)==pdTRUE) { }
}
static bool matches_ssid(const char *event_ssid,const char *target) {
    // Missing SSID cannot prove that this event belongs to the active Join.
    return event_ssid && event_ssid[0] && target && target[0] &&
           std::strcmp(event_ssid,target)==0;
}
// Single-owner Wi-Fi worker; no ESP-Hosted RPC from an IDF event callback.
static void handle_notices() {
    WifiNotice n = {};
    if (!notice_q) return;
    if (__atomic_exchange_n(&notice_overflow,0u,__ATOMIC_ACQ_REL)!=0u) {
        // Lost notifications invalidate the entire connection transition.
        // Never accept a delayed IP event after losing its parent event.
        discard_notices();
        reconnect=false;
        next_retry=0;
        sta_connected_notice=false;
        switching_network=false;
        disconnect_from_ssid[0]=0;
        pending_save=false;
        std::memset(pending_password,0,sizeof pending_password);
        std::memset(&staged_join,0,sizeof staged_join);
        staged_join_save=false;
        update_state("WIFI_EVENT_OVERFLOW",SIMHP4_WIFI_DISCONNECTED);
        return;
    }
    while (xQueueReceive(notice_q,&n,0)==pdTRUE) {
        if (switching_network) {
            // Never treat old AP's GOT_IP/CONNECTED as success for the new AP.
            if (n.kind==1 && matches_ssid(n.ssid,disconnect_from_ssid)) {
                switching_network=false;
                disconnect_from_ssid[0]=0;
                Request next = staged_join;
                std::memset(&staged_join,0,sizeof staged_join);
                begin_join_now(next.ssid,next.password,staged_join_save);
                staged_join_save=false;
                std::memset(&next,0,sizeof next);
            }
            continue;
        }
        if (n.kind==1) {
            // Intentional Leave/Forget must not be converted back into retry.
            if (!reconnect || !matches_ssid(n.ssid,pending_ssid)) continue;
            sta_connected_notice=false;
            char message[48];
            std::snprintf(message,sizeof message,"DISCONNECTED (%d)",n.reason);
            update_state(message,SIMHP4_WIFI_DISCONNECTED);
            if (n.reason==WIFI_REASON_AUTH_FAIL ||
                n.reason==WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                n.reason==WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY) {
                // Reason 210 is an AP security compatibility failure. A
                // blind 4s retry cannot fix this and repeatedly interrupts
                // the user; require an explicit next connection request.
                if (n.reason==WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY)
                    update_state("AP_SECURITY_MISMATCH_210",SIMHP4_WIFI_DISCONNECTED);
                reconnect=false;
                next_retry=0;
                pending_save=false;
                std::memset(pending_password,0,sizeof pending_password);
            } else {
                next_retry=xTaskGetTickCount()+pdMS_TO_TICKS(4000);
            }
        } else if (n.kind==3) {
            if (reconnect && matches_ssid(n.ssid,pending_ssid)) {
                sta_connected_notice=true;
                // An association is in progress. A previously scheduled retry
                // must never fire while the DHCP client is waiting for IPv4.
                next_retry=0;
                if (!snapshot.online)
                    update_state("WAITING_FOR_IP",SIMHP4_WIFI_CONNECTING);
            }
        } else if (n.kind==4) {
            // IDF reports LOST_IP after an expiry timer (not immediately on
            // disassociation). The station may still be associated; DHCP
            // recovery must not trigger a second esp_wifi_connect here.
            if (!reconnect || !sta_connected_notice) continue;
            next_retry=0;
            update_state("IP_LOST",SIMHP4_WIFI_CONNECTING);
        } else if (n.kind==2) {
            // GOT_IP does not contain SSID: require a matching CONNECTED
            // event from the current Join before accepting DHCP completion.
            if (!reconnect || !sta_connected_notice) continue;
            xSemaphoreTake(lock,portMAX_DELAY);
            snapshot.online=1;
            snapshot.link_state=SIMHP4_WIFI_IP_READY;
            std::snprintf(snapshot.ip,sizeof snapshot.ip,"%s",n.ip);
            snapshot.error[0]=0;
            ++snapshot.revision;
            xSemaphoreGive(lock);
            next_retry=0;
            ESP_LOGI(LOG,"STA GOT_IP %s",n.ip);
            r80_wifi_power_end();
            if(!r71_guestnet_start())
                ESP_LOGW(LOG,"R71 guest netif unavailable: BSD network stays isolated");
        }
    }
}
static bool success(esp_err_t e,const char*stage) {
    if (simhp4_sdio_fault_code()) {
        update_state("SDIO_INIT_FATAL",SIMHP4_WIFI_DISCONNECTED);
        return false; // do not start another RPC after a transport event
    }
    if(e==ESP_OK)return true;
    ESP_LOGW(LOG,"%s: %s (0x%X)",stage,esp_err_to_name(e),(unsigned)e);
    char message[48];
    std::snprintf(message,sizeof message,"%.22s 0x%X",stage,(unsigned)e);
    update_state(message,SIMHP4_WIFI_DISCONNECTED);
    return false;
}
static bool init_host() {
    // P4 GPIO/SDIO and C6 power must be available before any Wi-Fi remote API.
    auto &io=M5.getIOExpander(1); // Tab5 0x44 (PI4IOE5V6408), not 0x43
    // M5Unified's IOExpander_Base methods return void in the project's
    // installed version. Do not chain them or interpret their result as ACK.
    // PI4IOE5V6408 #1 (0x44), bit 0 is C6 power enable on Tab5.
    io.setHighImpedance(0,false);
    io.setDirection(0,true);
    io.digitalWrite(0,true);
    ESP_LOGI(LOG,"C6 power enable requested via PI4IOE5V6408 #1 bit0");
    vTaskDelay(pdMS_TO_TICKS(250));

    // Official ESP-Hosted 3.0.9 / Wi-Fi Remote 1.6.5: delayed startup.
    // Create the shared ESP event loop before connecting to the C6.
    esp_err_t e=nvs_flash_init();
    if (e==ESP_ERR_NVS_NO_FREE_PAGES || e==ESP_ERR_NVS_NEW_VERSION_FOUND) {
        update_state("NVS_REQUIRES_SERVICE",SIMHP4_WIFI_DISCONNECTED);
        return false; // never erase existing NVS
    }
    if(!success(e,"NVS_INIT"))return false;
    e=esp_netif_init();
    if(e!=ESP_OK && e!=ESP_ERR_INVALID_STATE)return success(e,"NETIF_INIT");
    e=esp_event_loop_create_default();
    if(e!=ESP_OK && e!=ESP_ERR_INVALID_STATE)return success(e,"EVENT_LOOP");
    if(!success(esp_event_handler_register(ESP_HOSTED_EVENT,ESP_EVENT_ANY_ID,
                          hosted_v3_event_cb,nullptr),"HOSTED_EVENTS"))return false;
    if (!success(esp_hosted_init(),"HOSTED_INIT"))return false;
    if (!success(esp_hosted_connect_to_slave(),"HOSTED_CONNECT")) {
        // Do not tear down the shared SDMMC host on unverified failure.
        return false;
    }
    if (simhp4_sdio_fault_code()) {
        update_state("SDIO_INIT_FATAL",SIMHP4_WIFI_DISCONNECTED);
        return false;
    }

    /* P4: feature auto-init in existing Wi-Fi worker, never on SDIO RX. */
    const int auto_rc=simhp4_r79_run_auto_init_on_wifi_worker();
    if (auto_rc != 0) {
        ESP_LOGE(LOG,"SIMHP4_R79_AUTOINIT_WORKER rc=%d",auto_rc);
        update_state("HOSTED_AUTO_INIT",SIMHP4_WIFI_DISCONNECTED);
        return false;
    }

    esp_netif_t* net=esp_netif_create_default_wifi_sta();
    if(!net){update_state("NETIF_CREATE",SIMHP4_WIFI_DISCONNECTED);return false;}
    sta_netif=net;
    wifi_init_config_t config=WIFI_INIT_CONFIG_DEFAULT();
    if(!success(esp_wifi_init(&config),"WIFI_INIT"))return false;
    if(!success(esp_wifi_set_storage(WIFI_STORAGE_RAM),"WIFI_STORAGE"))return false;
    if(!success(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event_cb,nullptr),"WIFI_EVENT"))return false;
    if(!success(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event_cb,nullptr),"IP_EVENT"))return false;
    if(!success(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_LOST_IP,event_cb,nullptr),"IP_LOST_EVENT"))return false;
    if(!success(esp_wifi_set_mode(WIFI_MODE_STA),"WIFI_STA"))return false;
    if(!success(esp_wifi_start(),"WIFI_START"))return false;
    /* REV10: quarter-dBm units: 56 = 14dBm, conservative and not mandatory. */
    const esp_err_t r80_tx_rc=esp_wifi_set_max_tx_power(56);
    if (r80_tx_rc!=ESP_OK)
        ESP_LOGW("SIMHP4_POWER","C6 TX limit 14dBm not applied (rc=0x%X); WiFi unchanged",(unsigned)r80_tx_rc);
    else
        ESP_LOGI("SIMHP4_POWER","C6 TX max request 14dBm accepted");
    xSemaphoreTake(lock,portMAX_DELAY);
    snapshot.driver_ready=1;
    snapshot.error[0]=0;
    ++snapshot.revision;
    xSemaphoreGive(lock);
    ESP_LOGI(LOG,"C6 STA ready via SDIO, P4 DHCP enabled");
    return true;
}
// The worker never changes an active connection's configuration until it
// has observed the old AP's explicit disconnected event. No timers are used
// to *force* a new connection after a missing disconnect acknowledgement.
static void begin_join_now(const char* ssid,const char* password,bool save) {
    r80_wifi_power_begin();
    if (!ssid || !ssid[0] || !password || std::strlen(ssid)>32 || std::strlen(password)>64) return;
    ScannedAP selected = {};
    const bool ap_observed = scanned_security_for(ssid,&selected);
    /* SIMHP4_R79_JOIN_EMPTY_PSK_GUARD: shared automatic and manual join.
     * Do not issue an empty-key Join to a known protected AP.
     * No security downgrades, no BSSID pinning, no credential logs.
     */
    if (ap_observed && selected.authmode != WIFI_AUTH_OPEN && !password[0]) {
        update_message("PASSWORD_REQUIRED");
        return;
    }
    /* Preserve R74 association policy: the scan record is evidence only.
     * Multiple BSSIDs may share an SSID; do not force security downgrade.
     */
    ESP_LOGI(LOG,"SIMHP4_R79_AP_AUTH_FLOW known=%d auth=%d channel=%d",
             (int)ap_observed, ap_observed?selected.authmode:-1,
             ap_observed?selected.channel:0);
    wifi_config_t cfg={};
    std::memcpy(cfg.sta.ssid,ssid,std::strlen(ssid));
    std::memcpy(cfg.sta.password,password,std::strlen(password));
    cfg.sta.threshold.authmode=WIFI_AUTH_OPEN;
    /* SIMHP4_R79_WIFI_SECURITY_POLICY: retain R74 open-minimum threshold.
     * Enable both SAE-PWE algorithms for WPA3 including H2E-only APs.
     * PMF-capable but not forced on WPA2 APs. No forced security downgrade.
     */
    cfg.sta.sae_pwe_h2e=WPA3_SAE_PWE_BOTH;
    cfg.sta.pmf_cfg.capable=true;
    cfg.sta.pmf_cfg.required=false;
    ESP_LOGI(LOG,"SIMHP4_R79_WIFI_SECURITY_POLICY pmf=capable sae=both password_present=%d",(int)(password[0]!=0));
    reconnect=false;
    sta_connected_notice=false;
    next_retry=0;
    if (success(esp_wifi_set_config(WIFI_IF_STA,&cfg),"SET_AP")) {
        std::snprintf(pending_ssid,sizeof pending_ssid,"%s",ssid);
        std::snprintf(pending_password,sizeof pending_password,"%s",password);
        pending_save=save;
        reconnect=true;
        xSemaphoreTake(lock,portMAX_DELAY);
        std::snprintf(snapshot.ssid,sizeof snapshot.ssid,"%s",ssid);
        ++snapshot.revision;
        xSemaphoreGive(lock);
        update_state("CONNECTING",SIMHP4_WIFI_CONNECTING);
        if (simhp4_sdio_fault_code()) {
            reconnect=false;pending_save=false;
            std::memset(pending_password,0,sizeof pending_password);
        } else if (!success(esp_wifi_connect(),"CONNECT")) {
            reconnect=false;
            pending_save=false;
            std::memset(pending_password,0,sizeof pending_password);
        }
    } else {
        pending_save=false;
        std::memset(pending_password,0,sizeof pending_password);
    }
    std::memset(&cfg,0,sizeof cfg);
}
static void do_connect(const char* ssid,const char* password,bool save) {
    if (!ssid || !ssid[0] || !password || std::strlen(ssid)>32 || std::strlen(password)>64) return;
    if (switching_network) {
        // Replace earlier queued Join, but preserve the current disconnect
        // barrier. The callback can never race with these worker-only writes.
        std::memset(&staged_join,0,sizeof staged_join);
        staged_join.type=Action::Join;
        std::snprintf(staged_join.ssid,sizeof staged_join.ssid,"%s",ssid);
        std::snprintf(staged_join.password,sizeof staged_join.password,"%s",password);
        staged_join_save=save;
        return;
    }
    bool has_active_link=false;
    xSemaphoreTake(lock,portMAX_DELAY);
    has_active_link=snapshot.online || snapshot.link_state==SIMHP4_WIFI_CONNECTING;
    xSemaphoreGive(lock);
    // A replacement join for the *same* SSID cannot be distinguished
    // reliably from late old-generation CONNECTED/GOT_IP notices: Wi-Fi
    // events carry no application join-attempt identifier. Fail closed
    // without changing the existing association or Wi-Fi credentials.
    // This explicitly blocks in-place password rotation until a verified
    // session generation/barrier design exists.
    if (matches_ssid(ssid,pending_ssid) && (reconnect || has_active_link)) {
        update_message("SAME_SSID_REJOIN_UNSAFE");
        return;
    }
    // Previous remote notices must not transition the new Join.
    discard_notices();
    if (has_active_link && pending_ssid[0]) {
        switching_network=true;
        sta_connected_notice=false;
        reconnect=false;
        next_retry=0;
        std::snprintf(disconnect_from_ssid,sizeof disconnect_from_ssid,"%s",pending_ssid);
        std::memset(&staged_join,0,sizeof staged_join);
        staged_join.type=Action::Join;
        std::snprintf(staged_join.ssid,sizeof staged_join.ssid,"%s",ssid);
        std::snprintf(staged_join.password,sizeof staged_join.password,"%s",password);
        staged_join_save=save;
        disconnect_deadline=xTaskGetTickCount()+pdMS_TO_TICKS(5000);
        update_state("SWITCHING",SIMHP4_WIFI_DISCONNECTED);
        if (!success(esp_wifi_disconnect(),"SWITCH_DISCONNECT")) {
            switching_network=false;
            std::memset(&staged_join,0,sizeof staged_join);
            std::memset(pending_password,0,sizeof pending_password);
            pending_save=false;
        }
        return;
    }
    begin_join_now(ssid,password,save);
}
// SIMHP4_R67_WORKER_QUARANTINE: after a fatal SDIO transport fault,
// never issue further Wi-Fi RPC or accept stale CONNECTED/GOT_IP events.
// This is safe offline containment, NOT a complete transport reset/recovery.
static void enter_sdio_quarantine(uint32_t code) {
    r71_guestnet_link(false);
    __atomic_store_n(&ready,false,__ATOMIC_RELEASE);
    reconnect=false;
    next_retry=0;
    sta_connected_notice=false;
    switching_network=false;
    disconnect_from_ssid[0]=0;
    pending_save=false;
    staged_join_save=false;
    std::memset(pending_password,0,sizeof pending_password);
    std::memset(&staged_join,0,sizeof staged_join);
    discard_notices();
    if (q) {
        Request dropped={};
        while (xQueueReceive(q,&dropped,0)==pdTRUE)
            std::memset(&dropped,0,sizeof dropped);
    }
    if (lock && xSemaphoreTake(lock,pdMS_TO_TICKS(100))==pdTRUE) {
        snapshot.driver_ready=0;
        snapshot.online=0;
        snapshot.link_state=SIMHP4_WIFI_DISCONNECTED;
        snapshot.ip[0]=0;
        std::snprintf(snapshot.error,sizeof snapshot.error,
                      "SDIO_FATAL_%lu",(unsigned long)code);
        ++snapshot.revision;
        xSemaphoreGive(lock);
    }
    ESP_LOGE(LOG,"SDIO fault=%lu, Wi-Fi disabled; BSD remains active",
             (unsigned long)code);
}
/* SIMHP4_R79_SAVED_CREDENTIAL_JOIN: the UI intentionally wipes its password
 * on AP selection. A blank CONNECT press must not overwrite or discard the
 * saved credentials of the identical protected network.
 * Exact SSID match and an observed non-open AP are mandatory. A newly entered
 * password always wins. The credential is neither displayed nor logged.
 */
static void join_from_ui_request(const char *ssid,const char *entered_password) {
    if (!ssid || !ssid[0] || !entered_password) return;
    if (entered_password[0]) {
        do_connect(ssid,entered_password,true);
        return;
    }
    ScannedAP ap={};
    if (!scanned_security_for(ssid,&ap)) {
        update_message("SELECT_SCANNED_AP");
        return;
    }
    if (ap.authmode == WIFI_AUTH_OPEN) {
        do_connect(ssid,"",true);
        return;
    }
    /* Never read another SSID's key, and never mutate NVS in this branch. */
    char saved_ssid[33]={},saved_pass[65]={};
    nvs_handle_t h;
    bool matched=false;
    if (nvs_open("simhp4wifi",NVS_READONLY,&h)==ESP_OK) {
        size_t n=sizeof saved_ssid,m=sizeof saved_pass;
        if (nvs_get_str(h,"ssid",saved_ssid,&n)==ESP_OK &&
            nvs_get_str(h,"pass",saved_pass,&m)==ESP_OK &&
            std::strcmp(saved_ssid,ssid)==0 && saved_pass[0]) matched=true;
        nvs_close(h);
    }
    if (matched) {
        ESP_LOGI(LOG,"SIMHP4_R79_SAVED_CREDENTIAL_JOIN exact_ssid=1 reused=1");
        do_connect(ssid,saved_pass,false); /* credential already persisted */
    } else {
        update_message("PASSWORD_REQUIRED");
    }
    /* Do not trust plain memset optimization to erase temporary credentials. */
    volatile char *wipe_key=saved_pass;
    for (size_t i=0;i<sizeof saved_pass;++i) wipe_key[i]=0;
    volatile char *wipe_ssid=saved_ssid;
    for (size_t i=0;i<sizeof saved_ssid;++i) wipe_ssid[i]=0;
}
static void worker(void*) {
    r80_wifi_power_begin();
    const bool init_ok = init_host();
    __atomic_store_n(&ready,init_ok,__ATOMIC_RELEASE);
    if (!init_ok) { r80_wifi_power_end(); ESP_LOGE(LOG,"WiFi unavailable; BSD operation unaffected");vTaskDelete(nullptr);return; }
    /* R76: Always populate SSIDs before restoring NVS credentials.
     * With credentials saved, the old code skipped its only automatic scan.
     * An early user tap could also be discarded while Hosted was starting.
     * This radio RPC runs ONLY here on the existing Wi-Fi worker, after
     * WIFI_START, never from the VAX CPU, event callback or splash UI.
     */
    {
        update_message("SCANNING");
        wifi_scan_config_t cfg={};
        const esp_err_t scan_rc=esp_wifi_scan_start(&cfg,true);
        if (simhp4_sdio_fault_code()) {
            enter_sdio_quarantine(simhp4_sdio_fault_code());
            vTaskDelete(nullptr);
            return;
        }
        if (scan_rc==ESP_OK) {
            uint16_t count=SIMHP4_WIFI_MAX_APS;
            wifi_ap_record_t aps[SIMHP4_WIFI_MAX_APS]={};
            const esp_err_t records_rc=esp_wifi_scan_get_ap_records(&count,aps);
            if (simhp4_sdio_fault_code()) {
                enter_sdio_quarantine(simhp4_sdio_fault_code());
                vTaskDelete(nullptr);
                return;
            }
            if (records_rc==ESP_OK) {
                retain_scanned_security(aps,count);
                xSemaphoreTake(lock,portMAX_DELAY);
                snapshot.ap_count=count;
                for (int i=0;i<count;i++) {
                    std::snprintf(snapshot.aps[i].ssid,sizeof snapshot.aps[i].ssid,"%.32s",(char*)aps[i].ssid);
                    snapshot.aps[i].rssi=aps[i].rssi;
                    snapshot.aps[i].secure=aps[i].authmode!=WIFI_AUTH_OPEN;
                }
                ++snapshot.revision;
                xSemaphoreGive(lock);
                update_message("READY");
                ESP_LOGI(LOG,"SIMHP4_R76_SCAN_STARTUP STA SCAN_DONE count=%u",(unsigned)count);
            } else success(records_rc,"SCAN_RECORDS");
        } else success(scan_rc,"SCAN_START");
    }
    bool restored_network = false;
    nvs_handle_t h;
    if(nvs_open("simhp4wifi",NVS_READONLY,&h)==ESP_OK) {
        char ssid[33]={},pw[65]={};size_t n=sizeof ssid, m=sizeof pw;
        if(nvs_get_str(h,"ssid",ssid,&n)==ESP_OK && nvs_get_str(h,"pass",pw,&m)==ESP_OK)
            { set_saved_identity(ssid); do_connect(ssid,pw,false); restored_network = true; }
        std::memset(pw,0,sizeof pw);
        nvs_close(h);
    }
    /* R76: first scan already completed before saved-NVS association.
     * Preserve explicit RE-SCAN requests and the normal queue processing. */
    (void)restored_network;
    for(;;) {
        const uint32_t transport_fault=simhp4_sdio_fault_code();
        if (transport_fault) {
            enter_sdio_quarantine(transport_fault);
            vTaskDelete(nullptr);
            return; /* unreachable in ESP-IDF */
        }
        handle_notices();
        Request req={};
        if(xQueueReceive(q,&req,pdMS_TO_TICKS(350))==pdTRUE) {
            if(req.type==Action::Scan) {
                update_message("SCANNING");
                wifi_scan_config_t cfg={};
                esp_err_t e=esp_wifi_scan_start(&cfg,true);
                /* SIMHP4_R67_AFTER_BLOCKING_RPC_GUARD */
                if (simhp4_sdio_fault_code()) {
                    std::memset(&req,0,sizeof req);
                    enter_sdio_quarantine(simhp4_sdio_fault_code());
                    vTaskDelete(nullptr);return;
                }
                if(e==ESP_OK) {
                    uint16_t count=SIMHP4_WIFI_MAX_APS;
                    wifi_ap_record_t aps[SIMHP4_WIFI_MAX_APS]={};
                    const esp_err_t records_rc=esp_wifi_scan_get_ap_records(&count,aps);
                    if(records_rc==ESP_OK) {
                        retain_scanned_security(aps,count);
                        xSemaphoreTake(lock,portMAX_DELAY);
                        snapshot.ap_count=count;
                        for(int i=0;i<count;i++) {
                            std::snprintf(snapshot.aps[i].ssid,sizeof snapshot.aps[i].ssid,"%.32s",(char*)aps[i].ssid);
                            snapshot.aps[i].rssi=aps[i].rssi;
                            snapshot.aps[i].secure=aps[i].authmode!=WIFI_AUTH_OPEN;
                        }
                        ++snapshot.revision;
                        xSemaphoreGive(lock);
                        update_message("READY");
                        ESP_LOGI(LOG,"STA SCAN_DONE count=%u",(unsigned)count);
                    } else {
                        success(records_rc,"SCAN_RECORDS");
                    }
                } else {
                    success(e,"SCAN_START");
                }
            } else if(req.type==Action::Join) join_from_ui_request(req.ssid,req.password);
            else if(req.type==Action::Leave) {
                reconnect=false;next_retry=0;pending_save=false;sta_connected_notice=false;
                switching_network=false;staged_join_save=false;
                std::memset(&staged_join,0,sizeof staged_join);
                std::memset(pending_password,0,sizeof pending_password);
                esp_wifi_disconnect();update_state("DISCONNECTED",SIMHP4_WIFI_DISCONNECTED);
            }
            else if(req.type==Action::Forget) {
                reconnect=false;next_retry=0;pending_save=false;sta_connected_notice=false;
                switching_network=false;staged_join_save=false;
                std::memset(&staged_join,0,sizeof staged_join);
                std::memset(pending_password,0,sizeof pending_password);esp_wifi_disconnect();
                nvs_handle_t k;if(nvs_open("simhp4wifi",NVS_READWRITE,&k)==ESP_OK){nvs_erase_key(k,"ssid");nvs_erase_key(k,"pass");nvs_commit(k);nvs_close(k);}
                set_saved_identity("");
                xSemaphoreTake(lock,portMAX_DELAY);snapshot.ssid[0]=0;++snapshot.revision;xSemaphoreGive(lock);
                update_state("FORGOTTEN",SIMHP4_WIFI_DISCONNECTED);
            }
            std::memset(&req,0,sizeof req);
        }
        handle_notices();
        if (switching_network &&
            (int32_t)(xTaskGetTickCount()-disconnect_deadline)>=0) {
            // Unacknowledged disconnect: never start a different SSID by guess.
            switching_network=false;
            disconnect_from_ssid[0]=0;
            staged_join_save=false;
            reconnect=false;
            pending_save=false;
            std::memset(&staged_join,0,sizeof staged_join);
            std::memset(pending_password,0,sizeof pending_password);
            update_state("SWITCH_TIMEOUT",SIMHP4_WIFI_DISCONNECTED);
        }
        if(pending_save) {
            simhp4_wifi_snapshot_t now={};simhp4_wifi_backend_snapshot(&now);
            if(now.online)save_network();
        }
        if(reconnect && next_retry && (int32_t)(xTaskGetTickCount()-next_retry)>=0) {
            next_retry=0;
            sta_connected_notice=false;
            if (!success(esp_wifi_connect(),"RECONNECT")) reconnect=false;
        }
    }
}
static void post(Action a,const char*ssid=nullptr,const char*password=nullptr) {
    if (!q || simhp4_sdio_fault_code()) return;
    // The UI must report the unavailable radio instead of silently ignoring taps.
    if (!__atomic_load_n(&ready, __ATOMIC_ACQUIRE)) {
        // Keep the actual HOSTED_INIT/WIFI_INIT failure instead of hiding it.
        if (lock && xSemaphoreTake(lock, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (!snapshot.error[0]) {
                std::snprintf(snapshot.error,sizeof(snapshot.error),"C6 STARTING");
                ++snapshot.revision;
            }
            xSemaphoreGive(lock);
        }
        return;
    }
    Request req={};req.type=a;
    if(ssid)std::snprintf(req.ssid,sizeof req.ssid,"%s",ssid);
    if(password)std::snprintf(req.password,sizeof req.password,"%s",password);
    if (xQueueSend(q,&req,0)!=pdTRUE) {
        ESP_LOGW(LOG,"command queue full");
        update_message("C6 BUSY");
    }
    std::memset(&req,0,sizeof req);
}
}
extern "C" int simhp4_wifi_backend_start(void) {
    if(q) return 1;
    lock=xSemaphoreCreateMutex();
    q=xQueueCreate(6,sizeof(Request));
    notice_q=xQueueCreate(16,sizeof(WifiNotice));
    if(!lock||!q||!notice_q)return 0;
    if(xTaskCreatePinnedToCore(worker,"simhp4_wifi_sta",8192,nullptr,1,nullptr,0)!=pdPASS){
        update_state("TASK_CREATE",SIMHP4_WIFI_DISCONNECTED);
        return 0;
    }
    return 1;
}
extern "C" int simhp4_wifi_backend_snapshot(simhp4_wifi_snapshot_t*dst) {
    if(!dst||!lock)return 0;
    if(xSemaphoreTake(lock,pdMS_TO_TICKS(10))!=pdTRUE)return 0;
    *dst=snapshot;
    xSemaphoreGive(lock);
    // The UI must not show a stale IP while the worker is still polling.
    const uint32_t fault=simhp4_sdio_fault_code();
    if (fault) {
        dst->online=0;
        dst->driver_ready=0;
        dst->link_state=SIMHP4_WIFI_DISCONNECTED;
        dst->ip[0]=0;
        std::snprintf(dst->error,sizeof dst->error,"SDIO_FATAL_%lu",(unsigned long)fault);
    }
    return 1;
}
extern "C" void simhp4_wifi_backend_scan(void){post(Action::Scan);}
extern "C" void simhp4_wifi_backend_join(const char*s,const char*p){post(Action::Join,s,p);}
extern "C" void simhp4_wifi_backend_leave(void){post(Action::Leave);}
extern "C" void simhp4_wifi_backend_forget(void){post(Action::Forget);}

/* UI-only export: cached saved network identity, not the NVS secret.
 * Caller runs on the existing presenter. Bounded wait avoids UI stalls.
 */
extern "C" int simhp4_wifi_backend_saved_ssid_copy(char *dst, size_t cap) {
    if (!dst || !cap) return 0;
    dst[0]=0;
    if (!lock || xSemaphoreTake(lock,pdMS_TO_TICKS(5))!=pdTRUE) return 0;
    std::snprintf(dst,cap,"%s",s_saved_identity);
    xSemaphoreGive(lock);
    return dst[0]!=0;
}
