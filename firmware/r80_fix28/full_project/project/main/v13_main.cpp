extern "C" int simhp4_r80_rev18_reboot_confirmed(void);
extern "C" int simhp4_r80_rev16_fsck_salvaged(void);
#include "esp_timer.h"
#include <M5Unified.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "simhp4_vax_keyboard.h"
#include "simhp4_vax_terminal.h"
#include "simhp4_wifi_overlay.h"

extern "C" {
#include "vax_defs.h"
#include "scp.h"
#include "sim_timer.h"
#include "sim_console.h"
#include "sim_disk.h"
#include "simhp4_sd.h"
#include "esp_heap_caps.h"
#include "esp_err.h"

/* SIMHP4_R14AB_R4R2_FAT_HEADER_FIX
 *
 * Do not include esp_vfs_fat.h in this SIMH/M5 C++ translation unit.
 * Its FatFs internals expose FF_MAX_SS -> MAX(...) here before MAX is
 * available, which breaks preprocessing. esp_vfs_fat_info() is a public
 * ESP-IDF API; declare only its C ABI and keep the existing fatfs component
 * linkage already used by simhp4_sd.
 */
extern "C" esp_err_t esp_vfs_fat_info(
    const char *base_path,
    uint64_t *out_total_bytes,
    uint64_t *out_free_bytes);

/* SIMHP4_V13_R7R4_V13MAIN_HEAP_FENCES
 * Evidence-only allocator-corruption locator.
 * BEGIN is emitted before heap metadata traversal, so if the checker itself
 * faults, the last BEGIN line is the first known-bad boundary. */
static void simhp4_r7r4_heap_fence(const char *stage)
{
    ESP_LOGI("SIMHP4_R7R4", "HEAP_CHECK %s BEGIN", stage);
    const int ok = heap_caps_check_integrity_all(true) ? 1 : 0;
    ESP_LOGI("SIMHP4_R7R4", "HEAP_CHECK %s END ok=%d", stage, ok);
}

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
    bool workcopy_ok;
    bool attach_ok;
    bool writable_ok;
    bool read0_ok;
    unsigned census_count;
    unsigned long long media_bytes;
    uint32_t sector0_fnv;
    char media_path[256];
    char work_path[256];
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
    M5.Display.setTextSize(1.70f);
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
    M5.Display.drawString("V13 / 4.3BSD PROTECTED WORK-COPY", 32, 118);

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
                 s.workcopy_ok && s.attach_ok && s.writable_ok ? "WORK RW OK" : "WAIT/FAIL",
                 s.workcopy_ok && s.attach_ok && s.writable_ok ? core() : warnc());
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
        "Original protected / guest writes use QUASWORK.DSK.", 58, h - 44);
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


/* SIMHP4_R14AB_PROTECTED_WORKCOPY_RW
 *
 * Preserve the configured source image as the immutable recovery copy.
 * Only /sdcard/QUASWORK.DSK is ever attached writable.
 * No verified manifest -> no writable attach.
 * Existing work file without a verified manifest is NEVER overwritten.
 */
static const char *R14AB_WORK_PATH = "/sdcard/QUASWORK.DSK";
static const char *R14AB_MANIFEST_PATH = "/sdcard/QUASWORK.R14AB";
static const char *R14AB_MANIFEST_TMP = "/sdcard/QUASWORK.R14AB.tmp";

struct R14ABSha256 {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t data[64];
    size_t datalen;
};

static uint32_t r14ab_rotr(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32u - n));
}

static void r14ab_sha_transform(R14ABSha256 *c, const uint8_t data[64])
{
    static const uint32_t k[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
    };
    uint32_t m[64];
    for (unsigned i = 0; i < 16; ++i) {
        const unsigned j = i * 4u;
        m[i] = ((uint32_t)data[j] << 24) |
               ((uint32_t)data[j+1] << 16) |
               ((uint32_t)data[j+2] << 8) |
               ((uint32_t)data[j+3]);
    }
    for (unsigned i = 16; i < 64; ++i) {
        const uint32_t s0 = r14ab_rotr(m[i-15], 7) ^ r14ab_rotr(m[i-15], 18) ^ (m[i-15] >> 3);
        const uint32_t s1 = r14ab_rotr(m[i-2], 17) ^ r14ab_rotr(m[i-2], 19) ^ (m[i-2] >> 10);
        m[i] = m[i-16] + s0 + m[i-7] + s1;
    }

    uint32_t a=c->state[0], b=c->state[1], cc=c->state[2], d=c->state[3];
    uint32_t e=c->state[4], f=c->state[5], g=c->state[6], h=c->state[7];
    for (unsigned i = 0; i < 64; ++i) {
        const uint32_t S1 = r14ab_rotr(e,6) ^ r14ab_rotr(e,11) ^ r14ab_rotr(e,25);
        const uint32_t ch = (e & f) ^ ((~e) & g);
        const uint32_t t1 = h + S1 + ch + k[i] + m[i];
        const uint32_t S0 = r14ab_rotr(a,2) ^ r14ab_rotr(a,13) ^ r14ab_rotr(a,22);
        const uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
        const uint32_t t2 = S0 + maj;
        h=g; g=f; f=e; e=d+t1; d=cc; cc=b; b=a; a=t1+t2;
    }
    c->state[0]+=a; c->state[1]+=b; c->state[2]+=cc; c->state[3]+=d;
    c->state[4]+=e; c->state[5]+=f; c->state[6]+=g; c->state[7]+=h;
}

static void r14ab_sha_init(R14ABSha256 *c)
{
    c->datalen = 0; c->bitlen = 0;
    c->state[0]=0x6a09e667u; c->state[1]=0xbb67ae85u;
    c->state[2]=0x3c6ef372u; c->state[3]=0xa54ff53au;
    c->state[4]=0x510e527fu; c->state[5]=0x9b05688cu;
    c->state[6]=0x1f83d9abu; c->state[7]=0x5be0cd19u;
}

static void r14ab_sha_update(R14ABSha256 *c, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        c->data[c->datalen++] = data[i];
        if (c->datalen == 64) {
            r14ab_sha_transform(c, c->data);
            c->bitlen += 512;
            c->datalen = 0;
        }
    }
}

static void r14ab_sha_final(R14ABSha256 *c, uint8_t out[32])
{
    size_t i = c->datalen;
    c->data[i++] = 0x80u;
    if (i > 56) {
        while (i < 64) c->data[i++] = 0;
        r14ab_sha_transform(c, c->data);
        i = 0;
    }
    while (i < 56) c->data[i++] = 0;
    c->bitlen += (uint64_t)c->datalen * 8u;
    for (unsigned j = 0; j < 8; ++j)
        c->data[63u-j] = (uint8_t)(c->bitlen >> (j * 8u));
    r14ab_sha_transform(c, c->data);
    for (unsigned j = 0; j < 8; ++j) {
        out[j*4u+0u] = (uint8_t)(c->state[j] >> 24);
        out[j*4u+1u] = (uint8_t)(c->state[j] >> 16);
        out[j*4u+2u] = (uint8_t)(c->state[j] >> 8);
        out[j*4u+3u] = (uint8_t)c->state[j];
    }
}

static void r14ab_sha_hex(const uint8_t digest[32], char hex[65])
{
    static const char tab[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i) {
        hex[i*2u] = tab[(digest[i] >> 4) & 0x0Fu];
        hex[i*2u+1u] = tab[digest[i] & 0x0Fu];
    }
    hex[64] = '\0';
}

/* SIMHP4_R14AB_R5_HASH_PROGRESS_RECOVERY
 * Visible/cooperative full-file SHA-256. Read-only for source and work.
 */
static bool r14ab_hash_file(const char *path, uint8_t digest[32],
                            unsigned long long *bytes_out)
{
    FILE *f = fopen(path, "rb");
    if (f == nullptr) {
        ESP_LOGE("SIMHP4_R14AB", "R5 HASH open failed path=%s", path);
        return false;
    }

    size_t buf_bytes = 1024u * 1024u;
    uint8_t *buf = (uint8_t *)heap_caps_malloc(
        buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == nullptr) {
        buf_bytes = 64u * 1024u;
        buf = (uint8_t *)heap_caps_malloc(
            buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (buf == nullptr) {
        buf_bytes = 8192u;
        buf = (uint8_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_8BIT);
    }
    if (buf == nullptr) {
        fclose(f);
        ESP_LOGE("SIMHP4_R14AB", "R5 HASH buffer allocation failed path=%s", path);
        return false;
    }

    struct stat st = {};
    unsigned long long expected = 0;
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
        expected = (unsigned long long)st.st_size;

    ESP_LOGI("SIMHP4_R14AB",
             "R5 HASH begin path=%s bytes=%llu buffer=%u",
             path, expected, (unsigned)buf_bytes);

    R14ABSha256 c;
    r14ab_sha_init(&c);
    unsigned long long total = 0;
    unsigned long long next_log = 8ULL * 1024ULL * 1024ULL;
    bool ok = true;

    for (;;) {
        const size_t n = fread(buf, 1, buf_bytes, f);
        if (n != 0) {
            r14ab_sha_update(&c, buf, n);
            total += (unsigned long long)n;
            while (total >= next_log) {
                ESP_LOGI("SIMHP4_R14AB",
                         "R5 HASH progress path=%s bytes=%llu/%llu mib=%llu/%llu",
                         path, total, expected,
                         total/(1024ULL*1024ULL),
                         expected/(1024ULL*1024ULL));
                next_log += 8ULL * 1024ULL * 1024ULL;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if (n < buf_bytes) {
            if (ferror(f)) ok = false;
            break;
        }
    }

    if (fclose(f) != 0) ok = false;
    heap_caps_free(buf);
    if (!ok) {
        ESP_LOGE("SIMHP4_R14AB", "R5 HASH failed path=%s bytes=%llu", path, total);
        return false;
    }

    r14ab_sha_final(&c, digest);
    char hex[65];
    r14ab_sha_hex(digest, hex);
    ESP_LOGI("SIMHP4_R14AB",
             "R5 HASH complete path=%s bytes=%llu sha256=%s",
             path, total, hex);

    if (bytes_out != nullptr) *bytes_out = total;
    return true;
}

static bool r14ab_copy_and_verify(const char *source, const char *work,
                                  unsigned long long expected_bytes,
                                  char source_hex[65])
{
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    const esp_err_t fi = esp_vfs_fat_info("/sdcard", &total_bytes, &free_bytes);
    if (fi != ESP_OK) {
        ESP_LOGE("SIMHP4_R14AB", "R3 FREE_SPACE preflight failed err=%d; refusing copy", (int)fi);
        return false;
    }

    const uint64_t reserve_bytes = 16ULL * 1024ULL * 1024ULL;
    ESP_LOGI("SIMHP4_R14AB",
             "R3 FREE_SPACE total=%llu free=%llu need=%llu reserve=%llu",
             (unsigned long long)total_bytes,
             (unsigned long long)free_bytes,
             expected_bytes,
             (unsigned long long)reserve_bytes);
    if (free_bytes < ((uint64_t)expected_bytes + reserve_bytes)) {
        ESP_LOGE("SIMHP4_R14AB",
                 "R3 WRITE_GATE CLOSED insufficient_free_space free=%llu need_with_reserve=%llu",
                 (unsigned long long)free_bytes,
                 (unsigned long long)((uint64_t)expected_bytes + reserve_bytes));
        return false;
    }

    FILE *in = fopen(source, "rb");
    if (in == nullptr) {
        ESP_LOGE("SIMHP4_R14AB", "source open failed path=%s", source);
        return false;
    }
    FILE *out = fopen(work, "wb");
    if (out == nullptr) {
        fclose(in);
        ESP_LOGE("SIMHP4_R14AB", "work create failed path=%s", work);
        return false;
    }

    size_t buf_bytes = 1024u * 1024u;
    uint8_t *buf = (uint8_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == nullptr) {
        buf_bytes = 64u * 1024u;
        buf = (uint8_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (buf == nullptr) {
        buf_bytes = 8192u;
        buf = (uint8_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_8BIT);
    }
    if (buf == nullptr) {
        fclose(out); fclose(in); (void)remove(work);
        ESP_LOGE("SIMHP4_R14AB", "R3 copy buffer allocation failed");
        return false;
    }

    ESP_LOGI("SIMHP4_R14AB", "R3 COPY_BUFFER bytes=%u psram_preferred=1", (unsigned)buf_bytes);

    R14ABSha256 src_ctx; r14ab_sha_init(&src_ctx);
    unsigned long long copied = 0;
    unsigned long long next_log = 1ULL * 1024ULL * 1024ULL;
    unsigned long long next_flush = 8ULL * 1024ULL * 1024ULL;
    bool ok = true;
    ESP_LOGI("SIMHP4_R14AB",
             "WORKCOPY_CREATE begin source=%s work=%s bytes=%llu original_write=0",
             source, work, expected_bytes);

    for (;;) {
        const size_t n = fread(buf, 1, buf_bytes, in);
        if (n != 0) {
            r14ab_sha_update(&src_ctx, buf, n);
            const size_t w = fwrite(buf, 1, n, out);
            if (w != n) {
                ok = false;
                ESP_LOGE("SIMHP4_R14AB",
                         "WORKCOPY_CREATE short write copied=%llu wanted=%u wrote=%u",
                         copied, (unsigned)n, (unsigned)w);
                break;
            }
            copied += (unsigned long long)n;

            while (copied >= next_log) {
                ESP_LOGI("SIMHP4_R14AB",
                         "WORKCOPY_CREATE progress bytes=%llu/%llu mib=%llu/%llu",
                         copied, expected_bytes,
                         copied / (1024ULL * 1024ULL),
                         expected_bytes / (1024ULL * 1024ULL));
                next_log += 1ULL * 1024ULL * 1024ULL;
            }

            if (copied >= next_flush) {
                if (fflush(out) != 0) {
                    ok = false;
                    ESP_LOGE("SIMHP4_R14AB", "R3 periodic fflush failed copied=%llu", copied);
                    break;
                }
                next_flush += 8ULL * 1024ULL * 1024ULL;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if (n < buf_bytes) {
            if (ferror(in)) ok = false;
            break;
        }
    }

    if (fflush(out) != 0) ok = false;
    if (fclose(out) != 0) ok = false;
    if (fclose(in) != 0) ok = false;
    heap_caps_free(buf);

    if (!ok || copied != expected_bytes) {
        ESP_LOGE("SIMHP4_R14AB",
                 "WORKCOPY_CREATE failed copied=%llu expected=%llu; deleting only work file",
                 copied, expected_bytes);
        (void)remove(work);
        return false;
    }

    uint8_t src_digest[32];
    r14ab_sha_final(&src_ctx, src_digest);
    r14ab_sha_hex(src_digest, source_hex);
    ESP_LOGI("SIMHP4_R14AB", "R3 SOURCE_HASH complete bytes=%llu sha256=%s", copied, source_hex);

    uint8_t work_digest[32]; unsigned long long work_bytes = 0;
    if (!r14ab_hash_file(work, work_digest, &work_bytes)) {
        ESP_LOGE("SIMHP4_R14AB", "WORKCOPY_VERIFY cannot hash work file");
        (void)remove(work); return false;
    }
    char work_hex[65]; r14ab_sha_hex(work_digest, work_hex);
    if ((work_bytes != expected_bytes) || (std::strcmp(source_hex, work_hex) != 0)) {
        ESP_LOGE("SIMHP4_R14AB",
                 "WORKCOPY_VERIFY mismatch src_bytes=%llu work_bytes=%llu src_sha256=%s work_sha256=%s; deleting only work file",
                 expected_bytes, work_bytes, source_hex, work_hex);
        (void)remove(work); return false;
    }
    ESP_LOGI("SIMHP4_R14AB",
             "WORKCOPY_VERIFY PASS bytes=%llu sha256=%s original_preserved=1",
             work_bytes, source_hex);
    return true;
}

static bool r14ab_write_manifest(const char *source, unsigned long long bytes, const char source_hex[65])
{
    (void)remove(R14AB_MANIFEST_TMP);
    FILE *f = fopen(R14AB_MANIFEST_TMP, "wb");
    if (f == nullptr) return false;
    const int n = fprintf(f,
        "SIMHP4_R14AB_WORK_V1\n"
        "source=%s\n"
        "work=%s\n"
        "bytes=%llu\n"
        "source_sha256=%s\n",
        source, R14AB_WORK_PATH, bytes, source_hex);
    bool ok = (n > 0) && (fflush(f) == 0) && (fclose(f) == 0);
    if (!ok) { (void)remove(R14AB_MANIFEST_TMP); return false; }
    (void)remove(R14AB_MANIFEST_PATH);
    if (rename(R14AB_MANIFEST_TMP, R14AB_MANIFEST_PATH) != 0) {
        (void)remove(R14AB_MANIFEST_TMP); return false;
    }
    return true;
}

static bool r14ab_read_manifest(char source[256], char work[256],
                                unsigned long long *bytes, char source_hex[65])
{
    FILE *f = fopen(R14AB_MANIFEST_PATH, "rb");
    if (f == nullptr) return false;
    char line[384]; bool ok = true;
    if (fgets(line, sizeof(line), f) == nullptr || std::strncmp(line, "SIMHP4_R14AB_WORK_V1", 23) != 0) ok = false;
    if (ok && (fgets(line, sizeof(line), f) == nullptr || std::sscanf(line, "source=%255[^\r\n]", source) != 1)) ok = false;
    if (ok && (fgets(line, sizeof(line), f) == nullptr || std::sscanf(line, "work=%255[^\r\n]", work) != 1)) ok = false;
    if (ok && (fgets(line, sizeof(line), f) == nullptr || std::sscanf(line, "bytes=%llu", bytes) != 1)) ok = false;
    if (ok && (fgets(line, sizeof(line), f) == nullptr || std::sscanf(line, "source_sha256=%64s", source_hex) != 1)) ok = false;
    if (fclose(f) != 0) ok = false;
    return ok;
}

/* SIMHP4_R14AB_R6R3_DIRECT_BOOT_LARGETEXT
 *
 * R5 already proved QUASWORK.DSK == QUASDISK.DSK:
 *   bytes  = 622932480
 *   sha256 = 7b925c21dac0c166341b57a63d08c6ab564185d9d7af8b74443c35c63b5421db
 *
 * Startup is deliberately short:
 *   - NO SHA-256 at boot
 *   - NO manifest dependency
 *   - NO automatic copy/recopy
 *   - ONLY /sdcard/QUASWORK.DSK may be attached as writable RQ0
 *
 * Constant-time startup guard only:
 *   QUASWORK.DSK must exist as a regular file with the proven byte size.
 */
static bool r14ab_prepare_workcopy(V12Status *s)
{
    if (s == nullptr)
        return false;

    std::snprintf(s->work_path, sizeof(s->work_path),
                  "%s", R14AB_WORK_PATH);
    s->workcopy_ok = false;

    unsigned long long work_bytes = 0;
    if (!regular_file_size(R14AB_WORK_PATH, &work_bytes)) {
        ESP_LOGE("SIMHP4_R14AB",
                 "R6R3 DIRECT_BOOT CLOSED work_missing path=%s",
                 R14AB_WORK_PATH);
        return false;
    }

    if (work_bytes != 622932480ULL) {
        ESP_LOGE("SIMHP4_R14AB",
                 "R6R3 DIRECT_BOOT CLOSED work_size=%llu expected=622932480",
                 work_bytes);
        return false;
    }

    s->media_bytes = work_bytes;
    s->workcopy_ok = true;

    ESP_LOGI("SIMHP4_R14AB",
             "R6R3 STARTUP_DIRECT no_hash=1 no_manifest=1 no_recopy=1 work=%s bytes=%llu prior_verified_sha256=7b925c21dac0c166341b57a63d08c6ab564185d9d7af8b74443c35c63b5421db",
             s->work_path, work_bytes);
    ESP_LOGI("SIMHP4_R14AB",
             "WRITE_GATE OPEN direct_boot=1 original_protected=1");
    return true;
}

static void try_r14ab_workcopy_rq_attach(V12Status *s)
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


    /* SIMHP4_R14V_SIM_FINIT
     *
     * Embedded simhP4 bypasses SCP main(), therefore Open SIMH's normal
     * sim_finit() host-I/O initialization must be invoked explicitly.
     *
     * Evidence:
     *   direct/attached LBA0 bytes FNV = C6B9E541
     *   sim_disk_rdsect LBA0 FNV      = FB63AB69
     * FB63AB69 is exactly the FNV of the same 512 bytes with every 16-bit
     * word byte-swapped.  sim_finit() sets sim_end correctly on ESP32-P4
     * little-endian hosts and also initializes sim_toffset_64.
     */
    {
        const int32 r14v_end = sim_finit();
        ESP_LOGI("SIMHP4_R14V",
                 "FIO_INIT sim_end=%ld sizeof_t_offset=%u expected_little_endian=1",
                 (long)r14v_end,
                 (unsigned)sizeof(t_offset));
        if (r14v_end != 1) {
            ESP_LOGE("SIMHP4_R14V",
                     "FIO_INIT unexpected endian=%ld; refusing disk attach",
                     (long)r14v_end);
            return;
        }
    }

    /* SIMHP4_R14S_FORCE_RQ0_SIMH_FORMAT
     * This changes only host container backend selection.
     * Container backend selection is unchanged; R14AB later attaches only the verified work copy writable.
     */
    {
        t_stat r14s_fmt =
            sim_disk_set_fmt(&rq_dev.units[0], 0, "SIMH", NULL);
        ESP_LOGI("SIMHP4_R14S",
                 "RQ0_FORMAT force=SIMH status=%ld flags=0x%08lX",
                 (long)r14s_fmt,
                 (unsigned long)rq_dev.units[0].flags);
        if (r14s_fmt != SCPE_OK) {
            ESP_LOGE("SIMHP4_R14S",
                     "RQ0_FORMAT force failed status=%ld; refusing AUTO attach",
                     (long)r14s_fmt);
            return;
        }
    }

    /* SIMHP4_R14AB_PROTECTED_WORKCOPY_RW
     * Original media is never writable-attached.  A verified work copy is
     * the only RQ0 target once the write gate opens.
     */
    if (!r14ab_prepare_workcopy(s)) {
        ESP_LOGE("SIMHP4_R14AB",
                 "WRITE_GATE CLOSED; original remains untouched and RQ0 is not attached");
        return;
    }

    sim_switches = saved_switches & ~SWMASK('R');

    ESP_LOGI("SIMHP4_R14AB",
             "RQ0_ATTACH begin mode=READ_WRITE_WORK source=%s work=%s",
             s->media_path, s->work_path);

    const t_stat a = rq_dev.attach(&rq_unit[0], s->work_path);

    sim_switches = saved_switches;

    s->attach_ok =
        (a == SCPE_OK) &&
        ((rq_unit[0].flags & UNIT_ATT) != 0);

    s->writable_ok =
        s->attach_ok &&
        ((rq_unit[0].flags & UNIT_RO) == 0);

    ESP_LOGI("SIMHP4_R14AB",
             "RQ0_ATTACH status=%ld attached=%d writable=%d "
             "capac=%llu filename=%s original_protected=1",
             (long)a,
             s->attach_ok ? 1 : 0,
             s->writable_ok ? 1 : 0,
             (unsigned long long)rq_unit[0].capac,
             rq_unit[0].filename ? rq_unit[0].filename : "(null)");
    simhp4_r7r4_heap_fence("RQ0_ATTACH");

    if (s->attach_ok && !s->writable_ok) {
        ESP_LOGE("SIMHP4_R14AB",
                 "SAFETY: work copy unexpectedly attached read-only; detaching");
        (void)rq_dev.detach(&rq_unit[0]);
        s->attach_ok = false;
        return;
    }

    if (!s->attach_ok)
        return;

    uint8 sector[512] = {};
    t_seccnt got = 0;

    /* SIMHP4_R14T_ATTACHED_FILE_IO_PROBE
     *
     * R14S proved RQ0 format bits == SIMH but sim_disk_rdsect LBA0 still
     * returned a zero sector.  Probe the exact attached FILE* itself before
     * sim_disk_rdsect() to determine whether the defect is below or inside
     * sim_disk_rdsect().
     */
    {
        UNIT *r14t_u = &rq_dev.units[0];
        FILE *r14t_fp = r14t_u->fileref;
        uint8_t r14t_std[512];
        uint8_t r14t_sim[512];
        memset(r14t_std, 0, sizeof(r14t_std));
        memset(r14t_sim, 0, sizeof(r14t_sim));

        const unsigned long r14t_fmt =
            ((unsigned long)r14t_u->flags >> UNIT_V_UF) & 3ul;
        long r14t_saved_pos = -1;
        if (r14t_fp != NULL)
            r14t_saved_pos = ftell(r14t_fp);

        ESP_LOGI("SIMHP4_R14T",
                 "ATTACHED meta fileref=%p flags=0x%08lX fmt=%lu capac=%lu saved_pos=%ld",
                 (void *)r14t_fp,
                 (unsigned long)r14t_u->flags,
                 r14t_fmt,
                 (unsigned long)r14t_u->capac,
                 r14t_saved_pos);

        int r14t_std_seek = -1;
        size_t r14t_std_n = 0;
        int r14t_std_err = -1;
        int r14t_std_eof = -1;
        if (r14t_fp != NULL) {
            clearerr(r14t_fp);
            r14t_std_seek = fseek(r14t_fp, 0L, SEEK_SET);
            if (r14t_std_seek == 0)
                r14t_std_n = fread(r14t_std, 1, sizeof(r14t_std), r14t_fp);
            r14t_std_err = ferror(r14t_fp);
            r14t_std_eof = feof(r14t_fp);
        }

        uint32_t r14t_std_fnv = 0x811C9DC5u;
        for (size_t r14t_i = 0; r14t_i < sizeof(r14t_std); ++r14t_i) {
            r14t_std_fnv ^= (uint32_t)r14t_std[r14t_i];
            r14t_std_fnv *= 0x01000193u;
        }

        ESP_LOGI("SIMHP4_R14T",
                 "ATTACHED_STD seek=%d bytes=%u ferror=%d feof=%d fnv1a32=%08lX first16=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X",
                 r14t_std_seek,
                 (unsigned)r14t_std_n,
                 r14t_std_err,
                 r14t_std_eof,
                 (unsigned long)r14t_std_fnv,
                 (unsigned)r14t_std[0],  (unsigned)r14t_std[1],
                 (unsigned)r14t_std[2],  (unsigned)r14t_std[3],
                 (unsigned)r14t_std[4],  (unsigned)r14t_std[5],
                 (unsigned)r14t_std[6],  (unsigned)r14t_std[7],
                 (unsigned)r14t_std[8],  (unsigned)r14t_std[9],
                 (unsigned)r14t_std[10], (unsigned)r14t_std[11],
                 (unsigned)r14t_std[12], (unsigned)r14t_std[13],
                 (unsigned)r14t_std[14], (unsigned)r14t_std[15]);

        int r14t_sim_seek = -1;
        size_t r14t_sim_n = 0;
        int r14t_sim_err = -1;
        int r14t_sim_eof = -1;
        if (r14t_fp != NULL) {
            clearerr(r14t_fp);
            r14t_sim_seek = sim_fseeko(r14t_fp, (t_offset)0, SEEK_SET);
            if (r14t_sim_seek == 0)
                r14t_sim_n = sim_fread(r14t_sim, 1, sizeof(r14t_sim), r14t_fp);
            r14t_sim_err = ferror(r14t_fp);
            r14t_sim_eof = feof(r14t_fp);
        }

        uint32_t r14t_sim_fnv = 0x811C9DC5u;
        for (size_t r14t_i = 0; r14t_i < sizeof(r14t_sim); ++r14t_i) {
            r14t_sim_fnv ^= (uint32_t)r14t_sim[r14t_i];
            r14t_sim_fnv *= 0x01000193u;
        }

        ESP_LOGI("SIMHP4_R14T",
                 "ATTACHED_SIM seek=%d bytes=%u ferror=%d feof=%d fnv1a32=%08lX first16=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X",
                 r14t_sim_seek,
                 (unsigned)r14t_sim_n,
                 r14t_sim_err,
                 r14t_sim_eof,
                 (unsigned long)r14t_sim_fnv,
                 (unsigned)r14t_sim[0],  (unsigned)r14t_sim[1],
                 (unsigned)r14t_sim[2],  (unsigned)r14t_sim[3],
                 (unsigned)r14t_sim[4],  (unsigned)r14t_sim[5],
                 (unsigned)r14t_sim[6],  (unsigned)r14t_sim[7],
                 (unsigned)r14t_sim[8],  (unsigned)r14t_sim[9],
                 (unsigned)r14t_sim[10], (unsigned)r14t_sim[11],
                 (unsigned)r14t_sim[12], (unsigned)r14t_sim[13],
                 (unsigned)r14t_sim[14], (unsigned)r14t_sim[15]);

        if ((r14t_fp != NULL) && (r14t_saved_pos >= 0)) {
            clearerr(r14t_fp);
            (void)fseek(r14t_fp, r14t_saved_pos, SEEK_SET);
        }
    }

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

    /* SIMHP4_R14R_RAWFILE_LBA0_CROSSCHECK
     *
     * Read the exact same SD file directly with stdio, bypassing SIMH's disk
     * layer. READ-ONLY. This distinguishes:
     *   - stale/different SD image
     *   - SIMH disk-layer read returning zero-filled data.
     */
    {
        uint8_t r14r_raw[512];
        memset(r14r_raw, 0, sizeof(r14r_raw));

        FILE *r14r_fp = fopen("/sdcard/QUASDISK.DSK", "rb");
        size_t r14r_n = 0;
        int r14r_close = -1;

        if (r14r_fp != NULL) {
            r14r_n = fread(r14r_raw, 1, sizeof(r14r_raw), r14r_fp);
            r14r_close = fclose(r14r_fp);
        }

        uint32_t r14r_fnv = 0x811C9DC5u;
        for (size_t r14r_i = 0; r14r_i < r14r_n; ++r14r_i) {
            r14r_fnv ^= (uint32_t)r14r_raw[r14r_i];
            r14r_fnv *= 0x01000193u;
        }

        ESP_LOGI("SIMHP4_R14R",
                 "RAWFILE_LBA0 fopen=%d bytes=%u fclose=%d fnv1a32=%08lX",
                 r14r_fp != NULL ? 1 : 0,
                 (unsigned)r14r_n,
                 r14r_close,
                 (unsigned long)r14r_fnv);

        ESP_LOGI("SIMHP4_R14R",
                 "RAWFILE_LBA0 first16=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X",
                 (unsigned)r14r_raw[0],  (unsigned)r14r_raw[1],
                 (unsigned)r14r_raw[2],  (unsigned)r14r_raw[3],
                 (unsigned)r14r_raw[4],  (unsigned)r14r_raw[5],
                 (unsigned)r14r_raw[6],  (unsigned)r14r_raw[7],
                 (unsigned)r14r_raw[8],  (unsigned)r14r_raw[9],
                 (unsigned)r14r_raw[10], (unsigned)r14r_raw[11],
                 (unsigned)r14r_raw[12], (unsigned)r14r_raw[13],
                 (unsigned)r14r_raw[14], (unsigned)r14r_raw[15]);

        ESP_LOGI("SIMHP4_R14R",
                 "EXPECT_WINDOWS_LBA0 fnv1a32=c6b9e541 first16=00-00-04-01-00-00-01-00-18-00-00-E7-00-81-00-00");
    }

    simhp4_r7r4_heap_fence("RQ0_LBA0");
}


/* SIMHP4_V13_BOOT_HELPER
 * First guest-execution stage after V12 read-only media PASS.
 *
 * - R14AB keeps the configured source image immutable and boots from a verified writable work copy.
 * - NVR is seeded in RAM from Open SIMH's own nvr_empty_valid image.
 * - No NVR file is attached/written.
 * - KA630 Break Enable is cleared so BOOT CPU follows the ROM autoboot path.
 * - run_cmd(RU_BOOT,"CPU") is the normal Open SIMH BOOT execution path.
 */
extern "C" {
extern DEVICE nvr_dev;
extern uint8 *nvr;
extern uint8 nvr_empty_valid[NVRSIZE];
extern t_bool ka_hltenab;
void simhp4_v13_activate_boot_devices(void);
t_stat nvr_reset(DEVICE *dptr);
void wtc_set_valid(void);
}


/* SIMHP4_R14M_EXACT_BOOT_DUA0_INJECT
 *
 * R14L established a stable KA630 console prompt (>>>), but A164-entered
 * BOOT/B DUA0 is echoed and then rejected as ?17 ILL CMD.
 *
 * Inject exactly:
 *   42 4F 4F 54 20 44 55 41 30 0D
 *   B  O  O  T     D  U  A  0  CR
 *
 * Trigger only after the post-diagnostic BDR sequence 5 -> 4 -> 3 -> 8.
 */
extern "C" int32 ka_bdr; /* SIMHP4_R14M */

/* SIMHP4_R14S_FORCE_RQ0_SIMH_FORMAT
 *
 * ESP32-P4 evidence:
 *   raw stdio /sdcard/QUASDISK.DSK LBA0 = c6b9e541
 *   SIMH AUTO backend LBA0             = 4d7705c5 (512 zero bytes)
 *
 * Open SIMH AUTO tries VHD, then RAW, then SIMH.  Force this regular image
 * file to the SIMH backend before attach so the ESP raw backend cannot claim it.
 */
extern "C" {
extern DEVICE rq_dev;
t_stat sim_disk_set_fmt(UNIT *uptr, int32 val, const char *cptr, void *desc);
}

/* SIMHP4_R14T_ATTACHED_FILE_IO_PROBE
 * Compare the already-attached RQ0 FILE* through libc stdio and SIMH's
 * sim_fio wrappers before the existing sim_disk_rdsect() LBA0 census.
 * READ-ONLY; original stream position is restored. */
extern "C" {
int sim_fseeko(FILE *st, t_offset offset, int whence);
size_t sim_fread(void *bptr, size_t size, size_t count, FILE *fptr);
int32 sim_finit(void);
}

/* SIMHP4_R14Q_POSTDIAG_ROM_NODELAY
 * Open SIMH KA630 ROM exposes UNIT_NODELAY as a supported ROM-unit mode.
 * We switch it on only after the power-up diagnostics have completed and
 * exact BOOT DUA0<CR> has been consumed from the proven host FIFO. */
extern "C" UNIT rom_unit;


/* SIMHP4_R14N_POST_BOOT_TRACE
 * Armed only after exact R14M BOOT DUA0<CR> has been enqueued.
 * RQ instrumentation reads this C-linkage flag. */
extern "C" {
volatile int simhp4_r14n_trace_arm = 0;
}

static void simhp4_r14m_boot_inject_task(void *arg)
{
    (void)arg;
    unsigned stage = 0;

    for (;;) {
        const unsigned code = ((unsigned)ka_bdr) & 0x0Fu;

        if ((stage == 0u) && (code == 5u))
            stage = 1u;
        else if ((stage == 1u) && (code == 4u))
            stage = 2u;
        else if ((stage == 2u) && (code == 3u))
            stage = 3u;
        else if ((stage == 3u) && (code == 8u)) {
            static const uint8_t cmd[] = {
                0x42, 0x4F, 0x4F, 0x54, 0x2F, 0x52, 0x35, 0x3A,
                0x32, 0x20, 0x44, 0x55, 0x41, 0x30, 0x0D
            }; /* SIMHP4_R6R23_SINGLEUSER_BOOT: R5=RB_SINGLE (0x2), no disk edit */

            vTaskDelay(pdMS_TO_TICKS(250));

            /* SIMHP4_R14P_FIFO_DEQUEUE_HANDSHAKE
             * Pace on the actual host FIFO consumer, not tti_unit.pos.
             * At most one injected command byte is pending at any time. */
            int ok = 1;
            unsigned delivered = 0;

            simhp4_r14n_trace_arm = 1;

            for (unsigned i = 0; i < sizeof(cmd); ++i) {
                unsigned waited_ms = 0;

                while (simhp4_vax_keyboard_pending_count() != 0u) {
                    if (waited_ms >= 10000u) {
                        ESP_LOGE("SIMHP4_R14P",
                                 "BYTE%u prewait TIMEOUT pending=%lu deq=%lu",
                                 i,
                                 (unsigned long)simhp4_vax_keyboard_pending_count(),
                                 (unsigned long)simhp4_vax_keyboard_dequeue_count());
                        ok = 0;
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(10));
                    waited_ms += 10u;
                }
                if (!ok) break;

                const uint32_t deq_before =
                    simhp4_vax_keyboard_dequeue_count();

                if (!simhp4_vax_keyboard_enqueue(cmd[i])) {
                    ESP_LOGE("SIMHP4_R14P",
                             "BYTE%u enqueue FAILED ch=0x%02X deq=%lu",
                             i, (unsigned)cmd[i],
                             (unsigned long)deq_before);
                    ok = 0;
                    break;
                }

                waited_ms = 0;
                while (simhp4_vax_keyboard_dequeue_count() == deq_before) {
                    if (waited_ms >= 10000u) {
                        ESP_LOGE("SIMHP4_R14P",
                                 "BYTE%u dequeue TIMEOUT ch=0x%02X pending=%lu deq=%lu",
                                 i, (unsigned)cmd[i],
                                 (unsigned long)simhp4_vax_keyboard_pending_count(),
                                 (unsigned long)simhp4_vax_keyboard_dequeue_count());
                        ok = 0;
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(10));
                    waited_ms += 10u;
                }
                if (!ok) break;

                ++delivered;
                ESP_LOGI("SIMHP4_R14P",
                         "BYTE%u dequeued ch=0x%02X ascii=%c wait_ms=%u deq=%lu pending=%lu",
                         i, (unsigned)cmd[i],
                         (cmd[i] >= 0x20 && cmd[i] <= 0x7E) ? (char)cmd[i] : '.',
                         waited_ms,
                         (unsigned long)simhp4_vax_keyboard_dequeue_count(),
                         (unsigned long)simhp4_vax_keyboard_pending_count());

                vTaskDelay(pdMS_TO_TICKS(20));
            }

            ESP_LOGI("SIMHP4_R14P",
                     "EXACT_BOOT fifo-paced bytes=%u/%u ok=%d hex=42-4F-4F-54-2F-52-35-3A-32-20-44-55-41-30-0D",
                     delivered, (unsigned)sizeof(cmd), ok);

            /* SIMHP4_R14Q_POSTDIAG_ROM_NODELAY
             *
             * Keep calibrated ROM timing through KA630 power-up diagnostics.
             * Once the exact command has been consumed, use Open SIMH's own
             * ROM NODELAY unit mode for the bootstrap path. This changes only
             * host wall-clock pacing of ROM reads; ROM bytes, guest registers,
             * Q22 map construction, RQ/MSCP semantics and RQ0 media stay
             * untouched.
             */
            if (ok && (delivered == sizeof(cmd))) {
                const uint32_t before = (uint32_t)rom_unit.flags;
                rom_unit.flags |= (1u << UNIT_V_UF);
                ESP_LOGI("SIMHP4_R14Q",
                         "ROM_NODELAY armed postdiag=1 postcmd=1 before=0x%08lX after=0x%08lX user_flag_bit=%u",
                         (unsigned long)before,
                         (unsigned long)((uint32_t)rom_unit.flags),
                         (unsigned)UNIT_V_UF);
            } else {
                ESP_LOGW("SIMHP4_R14Q",
                         "ROM_NODELAY not armed command_complete=%d delivered=%u/%u",
                         ok ? 1 : 0,
                         delivered,
                         (unsigned)sizeof(cmd));
            }

            /* SIMHP4_R14N_POST_BOOT_TRACE
             * No guest-state changes. Arm RQ logs and sample CPU state
             * once per second for 180 seconds after the exact command. */
            simhp4_r14n_trace_arm = 1;
            ESP_LOGI("SIMHP4_R14N",
                     "TRACE_ARM post exact BOOT DUA0 duration_s=180 cpu_period_ms=1000 rq_reg_limit=128 rq_svc_limit=128");

            for (unsigned sec = 0; sec <= 180u; ++sec) {
                /* SIMHP4_R14AB_R6R4_BLOCKER_PROOF
                 * Observer only.  When the proven post-clock-loss fixed PC is
                 * sampled, emit all architectural registers exactly once.
                 * No guest memory/register/device state is modified. */
                static bool r6r4_blocker_logged = false;
                const uint32 r6r4_pc = (uint32)R[15];

                ESP_LOGI("SIMHP4_R14N",
                         "CPU sec=%u PC=0x%08lX R0=0x%08lX R1=0x%08lX R2=0x%08lX R3=0x%08lX SP=0x%08lX PSL=0x%08lX BDR=0x%04lX",
                         sec,
                         (unsigned long)r6r4_pc,
                         (unsigned long)((uint32)R[0]),
                         (unsigned long)((uint32)R[1]),
                         (unsigned long)((uint32)R[2]),
                         (unsigned long)((uint32)R[3]),
                         (unsigned long)((uint32)R[14]),
                         (unsigned long)((uint32)PSL),
                         (unsigned long)((uint32)ka_bdr & 0xFFFFu));

                if (!r6r4_blocker_logged && r6r4_pc == 0x8000FB39u) {
                    r6r4_blocker_logged = true;
                    ESP_LOGE("SIMHP4_R6R4",
                             "BLOCKER_HIT PC=0x%08lX PSL=0x%08lX "
                             "R0=%08lX R1=%08lX R2=%08lX R3=%08lX "
                             "R4=%08lX R5=%08lX R6=%08lX R7=%08lX "
                             "R8=%08lX R9=%08lX R10=%08lX R11=%08lX "
                             "R12=%08lX R13=%08lX SP=%08lX",
                             (unsigned long)((uint32)R[15]),
                             (unsigned long)((uint32)PSL),
                             (unsigned long)((uint32)R[0]),
                             (unsigned long)((uint32)R[1]),
                             (unsigned long)((uint32)R[2]),
                             (unsigned long)((uint32)R[3]),
                             (unsigned long)((uint32)R[4]),
                             (unsigned long)((uint32)R[5]),
                             (unsigned long)((uint32)R[6]),
                             (unsigned long)((uint32)R[7]),
                             (unsigned long)((uint32)R[8]),
                             (unsigned long)((uint32)R[9]),
                             (unsigned long)((uint32)R[10]),
                             (unsigned long)((uint32)R[11]),
                             (unsigned long)((uint32)R[12]),
                             (unsigned long)((uint32)R[13]),
                             (unsigned long)((uint32)R[14]));
                }
                if (sec != 180u)
                    vTaskDelay(pdMS_TO_TICKS(1000));
            }

            ESP_LOGI("SIMHP4_R14N", "TRACE_DONE duration_s=180");
            vTaskDelete(NULL);
            return;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// SIMHP4_R6R16_CORE_LOAD_BEGIN
// FreeRTOS's idle runtime counter is specific to the calling CPU on IDF.
// Reading it on tasks pinned to both cores gives a wall-time utilization
// estimate without disturbing VAX instruction counting or guest clock.
#if !CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS || !CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER
#error R6R16 requires FreeRTOS runtime stats using ESP_TIMER
#endif
static void simhp4_r6r16_core_load_task(void *unused)
{
    (void)unused;
    const int core = xPortGetCoreID();
    configRUN_TIME_COUNTER_TYPE previous_idle = ulTaskGetIdleRunTimeCounter();
    int64_t previous_wall = esp_timer_get_time();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        const configRUN_TIME_COUNTER_TYPE current_idle = ulTaskGetIdleRunTimeCounter();
        const int64_t current_wall = esp_timer_get_time();
        // Cast to counter type intentionally handles 32-bit timer wrap.
        const configRUN_TIME_COUNTER_TYPE idle_us =
            static_cast<configRUN_TIME_COUNTER_TYPE>(current_idle - previous_idle);
        const int64_t wall_us = current_wall - previous_wall;
        previous_idle = current_idle;
        previous_wall = current_wall;
        if (wall_us <= 0) continue;
        uint32_t idle_pct = static_cast<uint32_t>(
            (100ULL * static_cast<uint64_t>(idle_us)) / static_cast<uint64_t>(wall_us));
        if (idle_pct > 100u) idle_pct = 100u;
        ESP_LOGI("SIMHP4_R6R16", "CORE%d busy=%lu%% idle=%lu%% window_ms=%lld",
                 core, (unsigned long)(100u - idle_pct),
                 (unsigned long)idle_pct, (long long)(wall_us / 1000));
    }
}
// SIMHP4_R6R16_CORE_LOAD_END

// SIMHP4_R6R27A_EVENT_GATE -- single-user / 5s, IRQ and R10 yield sampled; 100ms minimum sleep spacing; no hot-loop probes
extern "C" {
extern volatile uint32 simhp4_r6r27a_events, simhp4_r6r27a_pre_nonpositive;
extern volatile uint32 simhp4_r6r27a_after_le1, simhp4_r6r27a_after_2_99, simhp4_r6r27a_after_ge100;
extern volatile uint32 simhp4_r6r27a_event_us, simhp4_r6r27a_event_max_us;
extern volatile int32 simhp4_r6r27a_last_pre, simhp4_r6r27a_last_post;
extern volatile uint32 simhp4_r6r27a_last_pc, simhp4_r6r27a_last_status;
extern volatile uint32 simhp4_r6r27a_host_yield_n, simhp4_r6r27a_host_yield_us, simhp4_r6r27a_host_yield_max_us;
extern volatile uint32 simhp4_r6r27a_irq_all;
}
static void simhp4_r6r27a_trace_task(void *)
{
    uint32_t previous[10] = {};
    int64_t prior_wall = esp_timer_get_time();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        const int64_t now = esp_timer_get_time();
        const uint32_t current[10] = {
            __atomic_load_n(&simhp4_r6r27a_events, __ATOMIC_RELAXED),
            __atomic_load_n(&simhp4_r6r27a_pre_nonpositive, __ATOMIC_RELAXED),
            __atomic_load_n(&simhp4_r6r27a_after_le1, __ATOMIC_RELAXED),
            __atomic_load_n(&simhp4_r6r27a_after_2_99, __ATOMIC_RELAXED),
            __atomic_load_n(&simhp4_r6r27a_after_ge100, __ATOMIC_RELAXED),
            __atomic_load_n(&simhp4_r6r27a_event_us, __ATOMIC_RELAXED),
            __atomic_load_n(&simhp4_r6r27a_host_yield_n, __ATOMIC_RELAXED),
            __atomic_load_n(&simhp4_r6r27a_host_yield_us, __ATOMIC_RELAXED),
            __atomic_load_n(&simhp4_r6r27a_irq_all, __ATOMIC_RELAXED),
            __atomic_load_n(&simhp4_r6r27a_host_yield_max_us, __ATOMIC_RELAXED),
        };
        uint32_t d[10];
        for (unsigned j=0; j<10; ++j) { d[j] = current[j] - previous[j]; previous[j] = current[j]; }
        ESP_LOGI("SIMHP4_R6R27A", "EVENT_GATE ms=%lld calls=%lu pre_le0=%lu post_le1=%lu post_2_99=%lu post_ge100=%lu event_us=%lu max_us=%lu pre=%ld post=%ld PC=%08lx status=%lu host_yield_n=%lu host_yield_us=%lu host_yield_max_us=%lu irq_taken=%lu",
            (long long)((now-prior_wall)/1000),
            (unsigned long)d[0], (unsigned long)d[1],
            (unsigned long)d[2], (unsigned long)d[3], (unsigned long)d[4],
            (unsigned long)d[5], (unsigned long)simhp4_r6r27a_event_max_us,
            (long)simhp4_r6r27a_last_pre, (long)simhp4_r6r27a_last_post,
            (unsigned long)simhp4_r6r27a_last_pc, (unsigned long)simhp4_r6r27a_last_status,
            (unsigned long)d[6], (unsigned long)d[7],
            (unsigned long)current[9], (unsigned long)d[8]);
        prior_wall = now;
    }
}

/* SIMHP4_R80_REV19_SUSPEND_ONESHOT
 * One-shot hibernation, not an always-replayable disk rollback checkpoint.
 * Suspends CPU in native SIMH run loop, SAVE serializes RAM/CPU/devices, then
 * puts a small validated manifest beside snapshot on SD. Never saves during
 * active CPU execution or from display/FreeRTOS task.
 */
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include "esp_system.h"
extern "C" t_stat sim_save(FILE *); /* Open SIMH scp.c serializer */
extern "C" volatile t_bool stop_cpu;
extern "C" int32 sim_interval;
static volatile unsigned r19_susp_requested=0, r19_guest_running=0;
/* SIMHP4_R80_REV19_FIX4_SHORTPATH_DISK_DIR
 * A FATFS without long filename support returns EINVAL for the prior
 * SIMHP4_R19_HIBERNATE.tmp.  Names below are STRICT DOS 8.3.
 * Use the already-open writable RA82 disk's directory: never guess mount path.
 * Do not create a disk-image copy and do not overwrite the attached disk.
 */
static char R19_SNAP[384], R19_SNAP_TMP[384], R19_META[384], R19_META_TMP[384];
static char R19_FSCK_GUARD[384];
static bool r19_join_disk_dir(char *out,size_t cap,const char *disk,const char *leaf){
    if(!out || !cap || !disk || !disk[0] || !leaf)return false;
    const char *slash=strrchr(disk,'/');
    const char *backslash=strrchr(disk,'\\');
    if(backslash && (!slash || backslash>slash))slash=backslash;
    const size_t dirlen=slash?(size_t)(slash-disk)+1u:0u;
    const size_t leaflen=strlen(leaf);
    if(dirlen){
        if(dirlen+leaflen>=cap)return false;
        memcpy(out,disk,dirlen);
        memcpy(out+dirlen,leaf,leaflen+1u);
    }else{
        if(2u+leaflen>=cap)return false;
        out[0]='.';out[1]='/';memcpy(out+2,leaf,leaflen+1u);
    }
    return strcmp(out,disk)!=0;
}
static bool r19_resolve_paths(){
    const UNIT *u=&rq_unit[0];
    if(!(u->flags&UNIT_ATT)||!u->fileref||!u->filename||!u->filename[0]){
        ESP_LOGE("SIMHP4_R19","FIX4: RQ disk not attached; no snapshot");return false;
    }
    const char *disk=u->filename;
    const bool ok=
        r19_join_disk_dir(R19_SNAP,sizeof(R19_SNAP),disk,"S19SAVE.BIN") &&
        r19_join_disk_dir(R19_SNAP_TMP,sizeof(R19_SNAP_TMP),disk,"S19SAVE.TMP") &&
        r19_join_disk_dir(R19_META,sizeof(R19_META),disk,"S19META.DAT") &&
        r19_join_disk_dir(R19_META_TMP,sizeof(R19_META_TMP),disk,"S19META.TMP") &&
        r19_join_disk_dir(R19_FSCK_GUARD,sizeof(R19_FSCK_GUARD),disk,"S19FSCK.FLG");
    if(!ok)ESP_LOGE("SIMHP4_R19","FIX4: cannot form FAT83 paths for disk=%s",disk);
    return ok;
}
struct R19_Meta {
    char magic[24];
    char disk_path[256];
    uint64_t disk_size;
    int64_t disk_mtime;
    uint64_t snapshot_size;
    uint64_t snapshot_hash;
    uint64_t disk_sample_hash;
};
static uint64_t r19_mix(uint64_t v,const unsigned char *p,size_t n){
    for(size_t i=0;i<n;i++){v^=(uint64_t)p[i];v*=1099511628211ULL;}
    return v;
}
static bool r19_file_hash(const char *name,uint64_t *sz,uint64_t *hash){
    FILE *f=fopen(name,"rb"); if(!f)return false;
    uint64_t h=14695981039346656037ULL,bytes=0;
    unsigned char buf[8192];size_t n;
    while((n=fread(buf,1,sizeof(buf),f))!=0){h=r19_mix(h,buf,n);bytes+=n;}
    const bool ok=feof(f)!=0 && !ferror(f);
    fclose(f);if(!ok)return false;
    *sz=bytes;*hash=h;return true;
}
static bool r19_disk_identity(R19_Meta *m){
    UNIT *u=&rq_unit[0];
    if(!(u->flags&UNIT_ATT)||!u->fileref||!u->filename||!u->filename[0])return false;
    if(strlen(u->filename)>=sizeof(m->disk_path))return false;
    struct stat st;
    if(stat(u->filename,&st)!=0 || st.st_size<512)return false;
    const char *name=u->filename;
    /* Reuse the paused guest disk stream. A second open may fail with FAT
     * file locks when the same image is already attached read/write. */
    FILE *f=u->fileref;
    const long original_position=ftell(f);
    if(original_position<0)return false;
    uint64_t h=14695981039346656037ULL;
    unsigned char buf[4096];
    const uint64_t size=(uint64_t)st.st_size;
    const uint64_t offsets[3]={0,size/2u,size>4096u?size-4096u:0u};
    bool ok=true;
    for(int i=0;i<3 && ok;i++){
        if(fseek(f,(long)offsets[i],SEEK_SET)!=0){ok=false;break;}
        const size_t available=(size_t)((size-offsets[i])<sizeof(buf)?size-offsets[i]:sizeof(buf));
        const size_t nr=fread(buf,1,available,f);
        if(nr!=available){ok=false;break;} h=r19_mix(h,buf,nr);
    }
    if(fseek(f,original_position,SEEK_SET)!=0)ok=false;
    if(!ok)return false;
    memset(m,0,sizeof(*m));
    strcpy(m->magic,"SIMHP4_R19_HIBERNATE_V1");
    strcpy(m->disk_path,name);
    m->disk_size=size; m->disk_mtime=(int64_t)st.st_mtime;
    m->disk_sample_hash=h;
    return true;
}
static bool r19_meta_matches(const R19_Meta &record){
    R19_Meta now;
    if(!r19_disk_identity(&now))return false;
    return strcmp(record.magic,"SIMHP4_R19_HIBERNATE_V1")==0 &&
        strcmp(record.disk_path,now.disk_path)==0 &&
        record.disk_size==now.disk_size &&
        record.disk_mtime==now.disk_mtime &&
        record.disk_sample_hash==now.disk_sample_hash;
}
static bool r19_manifest_read(R19_Meta *m){
    FILE *f=fopen(R19_META,"rb");if(!f)return false;
    const bool ok=fread(m,sizeof(*m),1,f)==1 && fgetc(f)==EOF;
    fclose(f);if(!ok)return false;
    if(!r19_meta_matches(*m))return false;
    uint64_t size=0,hash=0;
    return r19_file_hash(R19_SNAP,&size,&hash) &&
        size>=1024*1024 && size==m->snapshot_size && hash==m->snapshot_hash;
}
extern "C" void simhp4_r19_suspend_request(void){
    if(!__atomic_load_n(&r19_guest_running,__ATOMIC_ACQUIRE))return;
    __atomic_store_n(&r19_susp_requested,1u,__ATOMIC_RELEASE);
    stop_cpu=TRUE; sim_interval=0;
}
static bool r19_save_hibernate(){
    /* Stop happened at instruction boundary. No guest code may run here. */
    if(!r19_resolve_paths())return false;
    ESP_LOGI("SIMHP4_R19","FIX4 FAT83 SAVE path=%s disk=%s",R19_SNAP_TMP,rq_unit[0].filename);
    if(!rq_unit[0].fileref)return false;
    if(fflush(rq_unit[0].fileref)!=0 || fsync(fileno(rq_unit[0].fileref))!=0){
        ESP_LOGE("SIMHP4_R19","disk flush failed errno=%d",errno);return false;
    }
    remove(R19_META);remove(R19_META_TMP);remove(R19_SNAP_TMP);
    /* SIMHP4_R80_REV19_FIX2_SAVE_WRITER
     * Write probe first; if SIMH's sim_fopen rejects an otherwise writable
     * pathname, use the identical serializer directly on an open stdio stream.
     */
    ESP_LOGI("SIMHP4_R19","SIMHP4_R80_REV19_FIX2_SAVE_WRITER READY");
    remove(R19_SNAP_TMP);
    errno=0;
    FILE *pre=fopen(R19_SNAP_TMP,"wb");
    if(!pre){
        ESP_LOGE("SIMHP4_R19","SAVE PREFLIGHT open failed path=%s errno=%d (%s)",
                 R19_SNAP_TMP,errno,strerror(errno));
        return false;
    }
    if(fclose(pre)!=0){
        ESP_LOGE("SIMHP4_R19","SAVE PREFLIGHT close failed errno=%d",errno);
        remove(R19_SNAP_TMP);return false;
    }
    errno=0;
    t_stat save_status=save_cmd(0,R19_SNAP_TMP);
    int save_errno=errno;
    if(save_status!=SCPE_OK){
        struct stat partial;
        const long long partial_bytes=stat(R19_SNAP_TMP,&partial)==0 ?
            (long long)partial.st_size:-1LL;
        ESP_LOGE("SIMHP4_R19","SAVE status=%ld text=%s errno=%d partial_bytes=%lld",
             (long)save_status,sim_error_text(save_status),save_errno,partial_bytes);
        if(save_status==SCPE_OPENERR){
            FILE *direct=fopen(R19_SNAP_TMP,"wb");
            if(direct){
                errno=0;
                t_stat attempt=sim_save(direct);
                const int err=errno;
                /* Upstream sim_save closes sfile itself on SCPE_MEM. */
                if(attempt!=SCPE_MEM && fclose(direct)!=0 && attempt==SCPE_OK)
                    attempt=SCPE_IOERR;
                ESP_LOGI("SIMHP4_R19","SIMH direct SAVE status=%ld errno=%d",
                       (long)attempt,err);
                if(attempt==SCPE_OK)save_status=SCPE_OK;
            }else{
                ESP_LOGE("SIMHP4_R19","SIMH direct SAVE open errno=%d",errno);
            }
        }
    }
    if(save_status!=SCPE_OK){remove(R19_SNAP_TMP);return false;}
    R19_Meta m;
    if(!r19_disk_identity(&m) ||
       !r19_file_hash(R19_SNAP_TMP,&m.snapshot_size,&m.snapshot_hash) ||
       m.snapshot_size<1024*1024){
        ESP_LOGE("SIMHP4_R19","snapshot/disk validation failed");remove(R19_SNAP_TMP);return false;
    }
    if(remove(R19_SNAP)!=0 && errno!=ENOENT){remove(R19_SNAP_TMP);return false;}
    if(rename(R19_SNAP_TMP,R19_SNAP)!=0){remove(R19_SNAP_TMP);return false;}
    FILE *mf=fopen(R19_META_TMP,"wb");if(!mf)return false;
    bool good=fwrite(&m,sizeof(m),1,mf)==1;
    good=(fflush(mf)==0)&&good;
    if(good)good=(fsync(fileno(mf))==0);
    if(fclose(mf)!=0)good=false;
    if(!good || rename(R19_META_TMP,R19_META)!=0){remove(R19_META_TMP);return false;}
    ESP_LOGW("SIMHP4_R19","SUSPENDED snapshot=%llu bytes disk=%s; power off/reset is now safe",
       (unsigned long long)m.snapshot_size,m.disk_path);
    return true;
}
static bool r19_probe_hibernate(){
    ESP_LOGI("SIMHP4_R19","SIMHP4_R80_REV19_SUSPEND_ONESHOT READY");
    if(!r19_resolve_paths())return false;
    R19_Meta m;
    if(!r19_manifest_read(&m)){
       /* No stale checkpoint can be accidentally replayed. */
       if(access(R19_META,F_OK)==0){ESP_LOGW("SIMHP4_R19","STALE SNAPSHOT -> cold boot");remove(R19_META);}
       return false;
    }
    ESP_LOGI("SIMHP4_R19","HIBERNATE VALID; bypass ROM/BSD boot");
    return true;
}
static t_stat r19_run_guest(int32 mode,const char *arg){
    for(;;){
        __atomic_store_n(&r19_guest_running,1u,__ATOMIC_RELEASE);
        t_stat result=run_cmd(mode,arg);
        __atomic_store_n(&r19_guest_running,0u,__ATOMIC_RELEASE);
        if(!__atomic_exchange_n(&r19_susp_requested,0u,__ATOMIC_ACQ_REL))return result;
        if(result!=SCPE_STOP){
            ESP_LOGW("SIMHP4_R19","SUSPEND ignored: stop status=%ld",(long)result);
            return result;
        }
        if(r19_save_hibernate()){
            /* CPU intentionally remains suspended until power off/reset. */
            for(;;)vTaskDelay(pdMS_TO_TICKS(1000));
        }
        ESP_LOGE("SIMHP4_R19","SUSPEND failed; continuing guest without snapshot");
        mode=RU_CONT;arg="";
    }
}
static t_stat r19_first_run(bool resumed){
    if(!resumed)return r19_run_guest(RU_BOOT,"CPU");
    /* One shot: invalidate metadata BEFORE restoring or executing guest. */
    if(remove(R19_META)!=0){
        ESP_LOGE("SIMHP4_R19","Cannot consume snapshot: refusing RESTORE");
        return r19_run_guest(RU_BOOT,"CPU");
    }
    const t_stat st=restore_cmd(0,R19_SNAP);
    if(st!=SCPE_OK){
        ESP_LOGE("SIMHP4_R19","RESTORE failed status=%ld: restarting cleanly",(long)st);
        esp_restart();
        for(;;)vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI("SIMHP4_R19","RESTORED: RUN CONT; no ROM, fsck or quota");
    return r19_run_guest(RU_CONT,"");
}

/* SIMHP4_R80_REV19_FIX3_COLD_ESP_RESTART
 * Recover a guest-requested restart by restarting the entire ESP/SIMH host,
 * not by run_cmd(RU_BOOT) in a KA630 state left by a repaired BSD fsck.
 * No guest disk byte modifications or fsck bypass.
 */
/* FIX4: R19_FSCK_GUARD resolved alongside snapshot paths (DOS 8.3). */
extern "C" void simhp4_r19_fix3_login_reached(void){
    if(!r19_resolve_paths())return;
    if(access(R19_FSCK_GUARD,F_OK)==0){
        if(remove(R19_FSCK_GUARD)==0)
            ESP_LOGI("SIMHP4_R19", "FIX3: login reached, fsck restart guard cleared");
    }
}
static bool r19_fix3_flush_guest_disk(void){
    FILE *f=rq_unit[0].fileref;
    if(!f){ESP_LOGE("SIMHP4_R19", "FIX3: missing disk stream, no restart");return false;}
    if(fflush(f)!=0){ESP_LOGE("SIMHP4_R19", "FIX3: disk fflush failed errno=%d",errno);return false;}
    if(fsync(fileno(f))!=0){ESP_LOGE("SIMHP4_R19", "FIX3: disk fsync failed errno=%d",errno);return false;}
    return true;
}
static bool r19_fix3_mark_fsck_once(void){
    if(!r19_resolve_paths())return false;
    if(access(R19_FSCK_GUARD,F_OK)==0){
        ESP_LOGE("SIMHP4_R19", "FIX3: repeated fsck HALT; refusing reboot loop");
        return false;
    }
    FILE *f=fopen(R19_FSCK_GUARD,"wb");
    if(!f){ESP_LOGE("SIMHP4_R19", "FIX3: cannot create fsck guard errno=%d",errno);return false;}
    const char payload[]="SIMHP4_R80_REV19_FIX3_FSCK_ONCE\n";
    bool ok=fwrite(payload,1,sizeof(payload)-1u,f)==sizeof(payload)-1u;
    if(ok)ok=fflush(f)==0;
    if(ok)ok=fsync(fileno(f))==0;
    if(fclose(f)!=0)ok=false;
    if(!ok){
        ESP_LOGE("SIMHP4_R19", "FIX3: cannot persist fsck guard errno=%d",errno);
        remove(R19_FSCK_GUARD);
    }
    return ok;
}
static void r19_fix3_cold_restart(bool explicit_reboot){
    if(!r19_fix3_flush_guest_disk())return;
    if(!explicit_reboot && !r19_fix3_mark_fsck_once())return;
    ESP_LOGW("SIMHP4_R19", "SIMHP4_R80_REV19_FIX3_COLD_ESP_RESTART %s; host reset, no nested KA630 ROM",
             explicit_reboot?"BSD_REBOOT":"FSCK_SALVAGED");
    esp_restart();
    for(;;)vTaskDelay(pdMS_TO_TICKS(1000));
}

static t_stat simhp4_v13_boot_quasijarus(void)
{
    /* SIMHP4_V13_R2_DEVICE_ACTIVATE
     * V12 has already passed with the exact CPU+RQ+NULL table.  From this
     * point onward switch to the minimal KA630 ROM boot device closure. */
    simhp4_v13_activate_boot_devices();
    ESP_LOGI("SIMHP4_V13",
             "V13 DEVICE_CLOSURE activated count=11 CPU/TLB/ROM/NVR/WTC/SYSD/QBA/CLK/TTI/TTO/RQ");
    simhp4_r7r4_heap_fence("DEVICE_CLOSURE");
    /* SIMHP4_V13_R3_TIMER_INIT
     * Normal Open SIMH startup initializes the timer subsystem before
     * reset_all(). The embedded V12 probe skipped that global startup path;
     * V13 now includes CLK, so initialize the standard timer subsystem before
     * run_cmd(RU_BOOT,"CPU") reaches clk_reset(). */
    ESP_LOGI("SIMHP4_V13", "V13 TIMER_INIT begin");
    const t_bool timer_init_failed = sim_timer_init();
    ESP_LOGI("SIMHP4_V13", "V13 TIMER_INIT done failed=%d",
             (int)timer_init_failed);
    simhp4_r7r4_heap_fence("TIMER_INIT_DONE");
    if (timer_init_failed) {
        ESP_LOGE("SIMHP4_V13",
                 "V13 TIMER_INIT failed; BOOT CPU not attempted");
        return SCPE_IERR;
    }
    /* SIMHP4_V13_R5_CONSOLE_INIT
     * Normal Open SIMH startup performs sim_ttinit() after sim_timer_init()
     * and before run_cmd()/sim_ttrun(). The embedded boot path must do the
     * same so runtty is derived from the actual ESP console state. */
    ESP_LOGI("SIMHP4_V13", "V13 CONSOLE_INIT begin");
    const t_stat console_init_status = sim_ttinit();
    ESP_LOGI("SIMHP4_V13", "V13 CONSOLE_INIT done status=%ld",
             (long)console_init_status);
    simhp4_r7r4_heap_fence("CONSOLE_INIT_DONE");
    if (console_init_status != SCPE_OK) {
        ESP_LOGE("SIMHP4_V13",
                 "V13 CONSOLE_INIT failed; BOOT CPU not attempted");
        return console_init_status;
    }

    /* SIMHP4_V13_R8_NVR_BOOT_HELPER_FENCES
     * R7R4 proved heap clean after CONSOLE_INIT_DONE but corrupt by
     * BOOT_CPU_ANNOUNCED.  Fence each exact V13 R1 NVR helper step. */
    t_stat r = nvr_reset(&nvr_dev);
    ESP_LOGI("SIMHP4_R8", "NVR_RESET returned status=%ld nvr=%p bytes=%u",
             (long)r, (void *)nvr, (unsigned)NVRSIZE);
    simhp4_r7r4_heap_fence("R8_AFTER_NVR_RESET");
    if ((r != SCPE_OK) || (nvr == nullptr)) {
        ESP_LOGE("SIMHP4_V13", "V13 NVR_RESET failed status=%ld nvr=%p",
                 (long)r, (void *)nvr);
        return (r != SCPE_OK) ? r : SCPE_MEM;
    }

    std::memcpy(nvr, nvr_empty_valid, NVRSIZE);
    simhp4_r7r4_heap_fence("R8_AFTER_NVR_MEMCPY");
    wtc_set_valid();
    simhp4_r7r4_heap_fence("R8_AFTER_WTC_VALID");

    /* KA630 console ROM: clear Break Enable / NOAUTOBOOT. */
    ka_hltenab = TRUE;
    simhp4_r7r4_heap_fence("R8_AFTER_HLTENAB_CLEAR");

    simhp4_r7r4_heap_fence("R8_PRE_BOOT_LOG");
    ESP_LOGI("SIMHP4_V13",
             "V13 BOOT_CPU begin guest_execution=1 disk=RQ0 mode=READ_WRITE_WORKCOPY original_protected=1 nvr=RAM autoboot=0");
    simhp4_r7r4_heap_fence("BOOT_CPU_ANNOUNCED");
    /* SIMHP4_V13_R6_PREBOOT_HEAP_FENCE
     * Check allocator metadata immediately before run_cmd() enters guest
     * execution. This distinguishes pre-existing host corruption from
     * corruption occurring during early KA630 ROM execution. */
    const bool r6_heap_preboot_ok = heap_caps_check_integrity_all(true);
    ESP_LOGI("SIMHP4_V13",
             "V13 R6 HEAP_PREBOOT ok=%d",
             r6_heap_preboot_ok ? 1 : 0);
    if (!r6_heap_preboot_ok) {
        ESP_LOGE("SIMHP4_V13",
                 "V13 R6 HEAP_PREBOOT failed; BOOT CPU not attempted");
        return SCPE_IERR;
    }

    /* SIMHP4_V14A_A164_TTI_BRIDGE
     * Start the already-proven Tab5 A164 host path only after every existing
     * V12/R9/R10/R13 pre-boot condition has passed.  This keeps the V12
     * baseline and KA630 setup untouched.  The producer writes ASCII bytes to
     * the host FIFO consumed by sim_os_poll_kbd(); no VAX device state is
     * touched from the host task. */
    const int r14_fifo_ok = simhp4_vax_keyboard_init();
    const int r14_a164_task_ok =
        r14_fifo_ok ? simhp4_a164_keyboard_start() : 0;

    ESP_LOGI("SIMHP4_R14",
             "KEYBOARD READY fifo=%d A164_task=%d A164_present=%d",
             r14_fifo_ok,
             r14_a164_task_ok,
             simhp4_a164_keyboard_present());

    const int r14_heap_ok =
        heap_caps_check_integrity_all(true) ? 1 : 0;
    ESP_LOGI("SIMHP4_R14",
             "HEAP_AFTER_KEYBOARD ok=%d",
             r14_heap_ok);
    if (!r14_heap_ok) {
        ESP_LOGE("SIMHP4_R14",
                 "keyboard startup damaged heap; BOOT CPU not attempted");
        return SCPE_IERR;
    }

    /* SIMHP4_V14C_LOCAL_VAX_TERMINAL
     * Host-only output mirror.  VAX TTO semantics remain upstream: the
     * standard sim_os_putchar() byte stream is copied into a private FIFO,
     * consumed by a low-priority M5GFX presenter on CPU0. */
    const int r14c_terminal_ok = simhp4_vax_terminal_start();
    ESP_LOGI("SIMHP4_R14C",
             "TERMINAL START ok=%d",
             r14c_terminal_ok);
    if (!r14c_terminal_ok) {
        ESP_LOGE("SIMHP4_R14C",
                 "terminal presenter unavailable; BOOT CPU not attempted");
        return SCPE_MEM;
    }

    const int r14c_heap_ok =
        heap_caps_check_integrity_all(true) ? 1 : 0;
    ESP_LOGI("SIMHP4_R14C",
             "HEAP_AFTER_TERMINAL ok=%d",
             r14c_heap_ok);
    if (!r14c_heap_ok) {
        ESP_LOGE("SIMHP4_R14C",
                 "terminal startup damaged heap; BOOT CPU not attempted");
        return SCPE_IERR;
    }


    /* SIMHP4_R14M_EXACT_BOOT_DUA0_INJECT
     * Start after the proven FIFO exists and before entering KA630 ROM. */
    const bool r19_resume_requested = r19_probe_hibernate();
    const BaseType_t r14m_inject_ok = r19_resume_requested ? pdPASS :
        xTaskCreatePinnedToCore(simhp4_r14m_boot_inject_task,
                                "r14m_boot",
                                3072,
                                NULL,
                                2,
                                NULL,
                                0);
    ESP_LOGI("SIMHP4_R14M",
             "single-user BOOT/R5:2 DUA0 injector armed ok=%d trigger=5-4-3-8",
             (r14m_inject_ok == pdPASS) ? 1 : 0);
    if (r14m_inject_ok != pdPASS) {
        ESP_LOGE("SIMHP4_R14M",
                 "injector task creation failed; BOOT CPU not attempted");
        return SCPE_IERR;
    }

    /* SIMHP4_R6R15_TTO_7B: preserve guest TTO; use stock 7-bit output conversion. */
    const t_stat r6r15_tto_7b = set_cmd(0, "TTO 7B");
    ESP_LOGI("SIMHP4_R6R15", "TTO 7B status=%ld", (long)r6r15_tto_7b);
    if (r6r15_tto_7b != SCPE_OK) {
        ESP_LOGE("SIMHP4_R6R15", "TTO 7B configuration failed; no boot");
        return r6r15_tto_7b;
    }
    /* SIMHP4_R6R16_CORE_LOAD: host-only CPU utilization + quieter logs.
     * Preserve R6R15 TTO, VAX CPU/clock, RQ and all guest behavior. */
    esp_log_level_set("SIMHP4_R14N_RQ", ESP_LOG_WARN);
    esp_log_level_set("SIMHP4_R14N", ESP_LOG_WARN);
    const BaseType_t r6r16_cpu0 = xTaskCreatePinnedToCore(
        simhp4_r6r16_core_load_task, "r6r16_core0", 3072, nullptr,
        3, nullptr, 0);
    const BaseType_t r6r16_cpu1 = xTaskCreatePinnedToCore(
        simhp4_r6r16_core_load_task, "r6r16_core1", 3072, nullptr,
        3, nullptr, 1);
    ESP_LOGI("SIMHP4_R6R16", "CORE_LOAD start cpu0=%d cpu1=%d period_ms=5000 source=FreeRTOS_IDLE",
             r6r16_cpu0 == pdPASS ? 1 : 0, r6r16_cpu1 == pdPASS ? 1 : 0);
    if (r6r16_cpu0 != pdPASS || r6r16_cpu1 != pdPASS)
        ESP_LOGE("SIMHP4_R6R16", "One or more load tasks unavailable; guest boot continues");
    /* SIMHP4_R6R27A_EVENT_GATE low-overhead boundary-only observation */
    const BaseType_t trace_task_ok = xTaskCreatePinnedToCore(
        simhp4_r6r27a_trace_task, "r6r27a_trace", 4096, nullptr, 1, nullptr, 1);
    ESP_LOGI("SIMHP4_R6R27A", "TRACE_BEGIN task=%d single_user=1 cpu_hotloop_unchanged=1 clock_unchanged=1 rq_unchanged=1 R10_yield_spacing_100ms=1 vax_irq_observed=1",
             trace_task_ok == pdPASS ? 1 : 0);
    /* SIMHP4_R6R30_NATIVE_IDLE2: official SIMH idle mode, stability 2 seconds.
     * No R6R19 custom idle function or simulated-instruction fast-forward.
     * Do not change guest clock, IRQ, RQ, disk, or 100ms R6R27A host yield.
     */
    const t_stat r6r30_idle_status = set_cmd(0, "CPU IDLE=QUASIJARUS:2");
    ESP_LOGI("SIMHP4_R6R30", "IDLE_MODE mode=QUASIJARUS stability_seconds=2 status=%ld native_sim_idle=1 custom_fastforward=0",
             (long)r6r30_idle_status);
    ESP_LOGI("SIMHP4_R80_REV18", "SIMHP4_R80_REV18_EXPLICIT_GUEST_REBOOT ACTIVE reboot+sync gate");
    const int64_t r80_rev16_boot_begin_us = esp_timer_get_time();
    r = r19_first_run(r19_resume_requested);
    /* SIMHP4_R80_REV16_FSCK_HALT_ONE_SHOT:
     * A repaired 4.3BSD root can intentionally halt at the end of fsck.
     * Reboot ONLY for an observed SALVAGED + STOP_HALT combination, once.
     * The first BOOT DUA0 injector is one-shot, so explicitly re-arm it.
     */
    const int64_t r80_rev16_boot_elapsed_us =
        esp_timer_get_time() - r80_rev16_boot_begin_us;
    /* SIMHP4_R80_REV18_EXPLICIT_GUEST_REBOOT: preserve REV16 salvage path.
     * Also honor clean root 'reboot' at any uptime, exactly once.
     * Never restart on STOP_HALT alone. */
    const bool r80_rev18_guest_requested_reboot =
        simhp4_r80_rev18_reboot_confirmed() != 0;
    /* FIX3: repaired fsck and explicit root reboot both require new host
     * initialization. A second run_cmd(RU_BOOT) fails ROM diag with STOP_LOOP.
     */
    if (r == STOP_HALT &&
        ((simhp4_r80_rev16_fsck_salvaged() &&
          r80_rev16_boot_elapsed_us >= 1000000LL &&
          r80_rev16_boot_elapsed_us <= 600000000LL) ||
         r80_rev18_guest_requested_reboot)) {
        if (simhp4_vax_keyboard_pending_count() != 0u) {
            ESP_LOGE("SIMHP4_R19", "FIX3: keyboard FIFO nonempty; no host reboot");
        } else {
            r19_fix3_cold_restart(r80_rev18_guest_requested_reboot);
        }
    }
    ESP_LOGW("SIMHP4_V13", "V13 BOOT_CPU returned status=%ld", (long)r);
    return r;
}

extern "C" void app_main(void)
{
    V12Status st = {};

    init_display();
    draw_screen(st, "STARTING");

    ESP_LOGI(TAG,
             "V12 BEGIN screen=1 media_write=WORK_COPY_ONLY guest_execution=1 sim_name=%s",
             sim_name);
    simhp4_r7r4_heap_fence("V12_BEGIN");

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
    simhp4_r7r4_heap_fence("CPU_RESET");

    draw_screen(st, st.reset_ok ? "OK" : "FAIL");

    const t_stat rq_st =
        rq_dev.reset ? rq_dev.reset(&rq_dev) : SCPE_NOFNC;

    st.rq_reset_ok = (rq_st == SCPE_OK);

    ESP_LOGI(TAG,
             "V12 RQ_RESET status=%ld ok=%d",
             (long)rq_st,
             st.rq_reset_ok ? 1 : 0);
    simhp4_r7r4_heap_fence("RQ_RESET");

    draw_screen(st, st.reset_ok ? "OK" : "FAIL");

    st.sd_ok = simhp4_sd_mount() != 0;

    ESP_LOGI(TAG,
             "V12 SD mounted=%d",
             st.sd_ok ? 1 : 0);
    simhp4_r7r4_heap_fence("SD_MOUNT");

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
            simhp4_r7r4_heap_fence("MEDIA_PATH");

            draw_screen(st, st.reset_ok ? "OK" : "FAIL");
            try_r14ab_workcopy_rq_attach(&st);
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
        st.workcopy_ok &&
        st.attach_ok &&
        st.writable_ok &&
        st.read0_ok) {
        ESP_LOGI(TAG,
                 "V12 RESULT=PASS reset=1 rq=1 sd=1 "
                 "protected_workcopy=1 writable_attach=1 lba0=1 bytes=%llu fnv1a32=%08lx",
                 st.media_bytes,
                 (unsigned long)st.sector0_fnv);
        simhp4_r7r4_heap_fence("V12_PASS");

        /* SIMHP4_V13_BOOT_CALL
         * Reached only through the existing V12 PASS branch. */
        ESP_LOGI("SIMHP4_V13",
                 "V13 V12_BASELINE_CONFIRMED; entering KA630 ROM");
        (void)simhp4_v13_boot_quasijarus();

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
                 "path=%d exists=%d work=%d attach=%d writable=%d read0=%d",
                 st.reset_ok ? 1 : 0,
                 st.rq_reset_ok ? 1 : 0,
                 st.sd_ok ? 1 : 0,
                 st.media_path_configured ? 1 : 0,
                 st.media_exists ? 1 : 0,
                 st.workcopy_ok ? 1 : 0,
                 st.attach_ok ? 1 : 0,
                 st.writable_ok ? 1 : 0,
                 st.read0_ok ? 1 : 0);
    }

    for (;;) {
        // R6R44: touch lives in the active terminal presenter task.
        // No M5.update() from the (normally unreachable) app_main tail.
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
