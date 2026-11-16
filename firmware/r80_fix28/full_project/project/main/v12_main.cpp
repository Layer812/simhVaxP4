#include <M5Unified.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {
#include "vax_defs.h"
#include "scp.h"
#include "sim_disk.h"
#include "simhp4_sd.h"
}

extern "C" DEVICE cpu_dev;
extern "C" UNIT cpu_unit;
extern "C" DEVICE rq_dev;
extern "C" UNIT rq_unit[];
extern "C" DEVICE *sim_devices[];
extern "C" char sim_name[];
extern "C" const char *sim_savename;
extern "C" uint32 *M;
extern "C" int32 sys_model;
extern "C" int32 sim_switches;

static const char *TAG = "SIMHP4_V12";
static const char *CFG_PATH = "/sdcard/VAXMEDIA.TXT";

struct V12Status {
    bool reset_ok;
    bool rq_reset_ok;
    bool sd_ok;
    bool media_path_configured;
    bool media_exists;
    bool attach_ok;
    bool readonly_ok;
    bool read0_ok;
    unsigned census_count;
    unsigned long long media_bytes;
    uint32_t sector0_fnv;
    char media_path[256];
};

static uint16_t bg()    { return M5.Display.color565(0, 5, 2); }
static uint16_t outer() { return M5.Display.color565(0, 42, 18); }
static uint16_t mid()   { return M5.Display.color565(0, 126, 56); }
static uint16_t core()  { return M5.Display.color565(112, 255, 164); }
static uint16_t warnc() { return M5.Display.color565(255, 210, 80); }
static uint16_t badc()  { return M5.Display.color565(255, 96, 96); }

static void neon_text(int x, int y, const char *text, float size)
{
    M5.Display.setTextSize(size);

    M5.Display.setTextColor(outer());
    M5.Display.drawString(text, x - 2, y);
    M5.Display.drawString(text, x + 2, y);
    M5.Display.drawString(text, x, y - 2);
    M5.Display.drawString(text, x, y + 2);

    M5.Display.setTextColor(mid());
    M5.Display.drawString(text, x - 1, y);
    M5.Display.drawString(text, x + 1, y);
    M5.Display.drawString(text, x, y - 1);
    M5.Display.drawString(text, x, y + 1);

    M5.Display.setTextColor(core());
    M5.Display.drawString(text, x, y);
}

static void neon_rect(int x, int y, int w, int h)
{
    M5.Display.drawRect(x - 2, y - 2, w + 4, h + 4, outer());
    M5.Display.drawRect(x - 1, y - 1, w + 2, h + 2, mid());
    M5.Display.drawRect(x, y, w, h, core());
}

static void draw_row(int y, const char *label, const char *value, uint16_t color)
{
    M5.Display.setTextSize(1.45f);
    M5.Display.setTextColor(mid());
    M5.Display.drawString(label, 62, y);
    M5.Display.setTextColor(color);
    M5.Display.drawString(value, 330, y);
}

static void draw_screen(const V12Status &s, const char *reset_text)
{
    const int w = M5.Display.width();
    const int h = M5.Display.height();

    M5.Display.fillScreen(bg());
    neon_text(30, 18, "simhP4", 3.0f);
    neon_text(30, 72, "MICROVAX II", 2.0f);

    M5.Display.setTextSize(1.35f);
    M5.Display.setTextColor(mid());
    M5.Display.drawString("V12 / 4.3BSD READ-ONLY MEDIA PROBE", 32, 118);

    neon_rect(28, 164, w - 56, h - 236);

    draw_row(205, "MODEL", "KA630 / VAX_630", core());
    draw_row(247, "GUEST RAM", "16 MiB", core());
    draw_row(289, "CPU RESET",
             s.reset_ok ? "OK" : reset_text,
             s.reset_ok ? core() : warnc());
    draw_row(331, "RQ/MSCP RESET",
             s.rq_reset_ok ? "OK" : "-",
             s.rq_reset_ok ? core() : mid());
    draw_row(373, "SD",
             s.sd_ok ? "MOUNTED" : "FAIL",
             s.sd_ok ? core() : badc());

    if (s.media_path_configured) {
        draw_row(415, "MEDIA FILE",
                 s.media_exists ? "FOUND" : "MISSING",
                 s.media_exists ? core() : badc());
        draw_row(457, "RQ0 ATTACH",
                 s.attach_ok && s.readonly_ok ? "READ-ONLY OK" : "WAIT/FAIL",
                 s.attach_ok && s.readonly_ok ? core() : warnc());
        draw_row(499, "LBA 0 READ",
                 s.read0_ok ? "OK" : "-",
                 s.read0_ok ? core() : mid());
    } else {
        draw_row(415, "MEDIA FILE", "NOT CONFIGURED", warnc());

        char census[48];
        std::snprintf(census, sizeof(census), "%u candidate file(s)",
                      s.census_count);
        draw_row(457, "SD CENSUS", census,
                 s.census_count ? core() : warnc());
        draw_row(499, "RQ0 ATTACH", "NOT ATTEMPTED", mid());
    }

    M5.Display.setTextSize(1.05f);
    M5.Display.setTextColor(mid());

    if (s.media_path[0] != '\0') {
        const size_t n = std::strlen(s.media_path);
        const char *p = s.media_path;
        if (n > 84)
            p = s.media_path + (n - 84);

        M5.Display.drawString(p, 58, 553);
    } else {
        M5.Display.drawString(
            "Set /sdcard/VAXMEDIA.TXT to the exact image path.",
            58, 553);
    }

    M5.Display.setTextSize(1.0f);
    M5.Display.setTextColor(mid());
    M5.Display.drawString(
        "PDP-7 Release Final source is untouched.", 58, h - 72);
    M5.Display.drawString(
        "Image writes=0 / guest instructions=0.", 58, h - 44);
}

static void init_display()
{
    auto cfg = M5.config();
    M5.begin(cfg);

    if (M5.Display.width() < M5.Display.height())
        M5.Display.setRotation(1);

    M5.Display.setSwapBytes(true);
    M5.Display.setBrightness(255);
}

static bool safe_sd_path(const char *path)
{
    if (path == nullptr)
        return false;
    if (std::strncmp(path, "/sdcard/", 8) != 0)
        return false;
    if (std::strstr(path, "..") != nullptr)
        return false;

    const size_t n = std::strlen(path);
    return n > 8 && n < 240;
}

static void trim_line(char *s)
{
    if (s == nullptr)
        return;

    size_t n = std::strlen(s);
    while (n != 0 &&
           (s[n - 1] == '\r' || s[n - 1] == '\n' ||
            s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }

    size_t lead = 0;
    while (s[lead] == ' ' || s[lead] == '\t')
        ++lead;

    if (lead != 0)
        std::memmove(s, s + lead, std::strlen(s + lead) + 1);
}

static bool read_media_config(char *dst, size_t cap)
{
    FILE *fp = std::fopen(CFG_PATH, "rb");
    if (fp == nullptr)
        return false;

    const bool got = std::fgets(dst, (int)cap, fp) != nullptr;
    std::fclose(fp);

    if (!got)
        return false;

    trim_line(dst);
    return safe_sd_path(dst);
}

static bool regular_file_size(const char *path, unsigned long long *size_out)
{
    struct stat st = {};
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
        return false;

    if (size_out != nullptr)
        *size_out = (unsigned long long)st.st_size;
    return true;
}

static void census_dir(const char *path, int depth, V12Status *s)
{
    if (depth > 3 || s == nullptr)
        return;

    DIR *d = opendir(path);
    if (d == nullptr)
        return;

    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        if (std::strcmp(ent->d_name, ".") == 0 ||
            std::strcmp(ent->d_name, "..") == 0)
            continue;

        char full[320];
        if (std::snprintf(full, sizeof(full), "%s/%s",
                          path, ent->d_name) >= (int)sizeof(full))
            continue;

        struct stat st = {};
        if (stat(full, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode)) {
            census_dir(full, depth + 1, s);
            continue;
        }

        if (!S_ISREG(st.st_mode))
            continue;

        if ((unsigned long long)st.st_size < (1024ULL * 1024ULL))
            continue;

        ++s->census_count;
        ESP_LOGI(TAG,
                 "V12 MEDIA_CANDIDATE n=%u bytes=%llu path=%s",
                 s->census_count,
                 (unsigned long long)st.st_size,
                 full);
    }

    closedir(d);
}

static uint32_t fnv1a32(const uint8 *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static void try_readonly_rq_attach(V12Status *s)
{
    if (s == nullptr ||
        !s->media_path_configured ||
        !s->media_exists)
        return;

    if (rq_dev.attach == nullptr || rq_dev.detach == nullptr) {
        ESP_LOGE(TAG, "V12 RQ attach/detach pointer unavailable");
        return;
    }

    const int32 saved_switches = sim_switches;
    sim_switches = saved_switches | SWMASK('R');

    ESP_LOGI(TAG,
             "V12 RQ0_ATTACH begin mode=READ_ONLY path=%s",
             s->media_path);

    const t_stat a = rq_dev.attach(&rq_unit[0], s->media_path);

    sim_switches = saved_switches;

    s->attach_ok =
        (a == SCPE_OK) &&
        ((rq_unit[0].flags & UNIT_ATT) != 0);

    s->readonly_ok =
        s->attach_ok &&
        ((rq_unit[0].flags & UNIT_RO) != 0);

    ESP_LOGI(TAG,
             "V12 RQ0_ATTACH status=%ld attached=%d readonly=%d "
             "capac=%llu filename=%s",
             (long)a,
             s->attach_ok ? 1 : 0,
             s->readonly_ok ? 1 : 0,
             (unsigned long long)rq_unit[0].capac,
             rq_unit[0].filename ? rq_unit[0].filename : "(null)");

    if (s->attach_ok && !s->readonly_ok) {
        ESP_LOGE(TAG,
                 "V12 SAFETY: attach was not read-only; detaching immediately");
        (void)rq_dev.detach(&rq_unit[0]);
        s->attach_ok = false;
        return;
    }

    if (!s->attach_ok)
        return;

    uint8 sector[512] = {};
    t_seccnt got = 0;

    const t_stat r =
        sim_disk_rdsect(&rq_unit[0], 0, sector, &got, 1);

    s->read0_ok = (r == SCPE_OK) && (got == 1);
    if (s->read0_ok)
        s->sector0_fnv = fnv1a32(sector, sizeof(sector));

    ESP_LOGI(TAG,
             "V12 RQ0_LBA0 status=%ld sectors=%lu fnv1a32=%08lx",
             (long)r,
             (unsigned long)got,
             (unsigned long)s->sector0_fnv);
}

extern "C" void app_main(void)
{
    V12Status st = {};

    init_display();
    draw_screen(st, "STARTING");

    ESP_LOGI(TAG,
             "V12 BEGIN screen=1 media_write=0 guest_execution=0 sim_name=%s",
             sim_name);

    const t_stat cpu_st =
        cpu_dev.reset ? cpu_dev.reset(&cpu_dev) : SCPE_NOFNC;

    st.reset_ok =
        (cpu_st == SCPE_OK) &&
        (std::strcmp(sim_name, "MicroVAX II (KA630)") == 0) &&
        (sim_savename != nullptr) &&
        (std::strcmp(sim_savename, "MicroVAX II (KA630)") == 0) &&
        ((unsigned long long)cpu_unit.capac == (1ULL << 24)) &&
        (M != nullptr) &&
        (sys_model == 0) &&
        (sim_devices[0] == &cpu_dev) &&
        (sim_devices[1] == &rq_dev);

    ESP_LOGI(TAG,
             "V12 CPU_RESET status=%ld ok=%d memory=%s capacity=%llu",
             (long)cpu_st,
             st.reset_ok ? 1 : 0,
             M ? "allocated" : "null",
             (unsigned long long)cpu_unit.capac);

    draw_screen(st, st.reset_ok ? "OK" : "FAIL");

    const t_stat rq_st =
        rq_dev.reset ? rq_dev.reset(&rq_dev) : SCPE_NOFNC;

    st.rq_reset_ok = (rq_st == SCPE_OK);

    ESP_LOGI(TAG,
             "V12 RQ_RESET status=%ld ok=%d",
             (long)rq_st,
             st.rq_reset_ok ? 1 : 0);

    draw_screen(st, st.reset_ok ? "OK" : "FAIL");

    st.sd_ok = simhp4_sd_mount() != 0;

    ESP_LOGI(TAG,
             "V12 SD mounted=%d",
             st.sd_ok ? 1 : 0);

    draw_screen(st, st.reset_ok ? "OK" : "FAIL");

    if (st.sd_ok) {
        char path[sizeof(st.media_path)] = {};

        if (read_media_config(path, sizeof(path))) {
            st.media_path_configured = true;
            std::snprintf(st.media_path,
                          sizeof(st.media_path),
                          "%s", path);

            st.media_exists =
                regular_file_size(st.media_path, &st.media_bytes);

            ESP_LOGI(TAG,
                     "V12 MEDIA_PATH source=%s exists=%d bytes=%llu path=%s",
                     CFG_PATH,
                     st.media_exists ? 1 : 0,
                     st.media_bytes,
                     st.media_path);

            draw_screen(st, st.reset_ok ? "OK" : "FAIL");
            try_readonly_rq_attach(&st);
        } else {
            census_dir("/sdcard", 0, &st);

            ESP_LOGI(TAG,
                     "V12 MEDIA_PATH not configured; "
                     "config=%s candidates=%u",
                     CFG_PATH,
                     st.census_count);
        }
    }

    draw_screen(st, st.reset_ok ? "OK" : "FAIL");

    if (st.reset_ok &&
        st.rq_reset_ok &&
        st.sd_ok &&
        st.media_path_configured &&
        st.media_exists &&
        st.attach_ok &&
        st.readonly_ok &&
        st.read0_ok) {
        ESP_LOGI(TAG,
                 "V12 RESULT=PASS reset=1 rq=1 sd=1 "
                 "readonly_attach=1 lba0=1 bytes=%llu fnv1a32=%08lx",
                 st.media_bytes,
                 (unsigned long)st.sector0_fnv);
    } else if (st.reset_ok &&
               st.rq_reset_ok &&
               st.sd_ok &&
               !st.media_path_configured) {
        ESP_LOGI(TAG,
                 "V12 RESULT=READY reset=1 rq=1 sd=1 "
                 "media_path_needed=1 candidates=%u",
                 st.census_count);
    } else {
        ESP_LOGW(TAG,
                 "V12 RESULT=INCOMPLETE reset=%d rq=%d sd=%d "
                 "path=%d exists=%d attach=%d ro=%d read0=%d",
                 st.reset_ok ? 1 : 0,
                 st.rq_reset_ok ? 1 : 0,
                 st.sd_ok ? 1 : 0,
                 st.media_path_configured ? 1 : 0,
                 st.media_exists ? 1 : 0,
                 st.attach_ok ? 1 : 0,
                 st.readonly_ok ? 1 : 0,
                 st.read0_ok ? 1 : 0);
    }

    for (;;) {
        M5.update();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
