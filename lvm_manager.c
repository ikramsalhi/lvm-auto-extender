#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/stat.h>   // for mkdir()

// ===== CONFIGURABLES =====
static int DRY_RUN = 0;               // 1 = dry-run, 0 = real mode
static const int THRESHOLD_PCT = 80; // Trigger threshold (%)
static const int LOW_PCT = 40;       // Donor eligibility (< this %)
static const char *FALLBACK_DEV = "/dev/sdc";
static const char *WRITER_BASE_PATH = "/mnt/lv_home";
static const int WRITER_STOP_PCT = 95;

// Helper: run shell command
int run_cmd(const char *cmd) {
    printf("  [CMD] %s\n", cmd);
    if (DRY_RUN) {
        printf("  → 🟨 DRY-RUN: skipped\n");
        return 0;
    }
    int ret = system(cmd);
    if (ret == 0) {
        printf("  → ✅ Success\n");
    } else {
        printf("  → ❌ Failed (exit %d)\n", WEXITSTATUS(ret));
    }
    return ret;
}

// Helper: get filesystem usage %
int get_usage_pct(const char *path) {
    struct statvfs buf;
    if (statvfs(path, &buf) != 0) return -1;
    double used = (double)(buf.f_blocks - buf.f_bfree);
    double total = (double)buf.f_blocks;
    return (int)(100.0 * used / total + 0.5);
}

// ===== WRITER THREAD =====
void *writer_thread(void *arg) {
    char dir[256];
    snprintf(dir, sizeof(dir), "%s/writer", WRITER_BASE_PATH);
    run_cmd("mkdir -p /mnt/lv_home/writer");

    int count = 1;
    while (1) {
        int pct = get_usage_pct(WRITER_BASE_PATH);
        if (pct < 0 || pct >= WRITER_STOP_PCT) break;

        char cmd[512];
        snprintf(cmd, sizeof(cmd),
                 "dd if=/dev/zero of=%s/file_%03d.bin bs=1M count=500 2>/dev/null",
                 dir, count++);
        run_cmd(cmd);
        printf(".Created file_%03d.bin (500MB)\n", count-1);
        sleep(10);
    }
    printf(".Writer stopped at %d%%\n", get_usage_pct(WRITER_BASE_PATH));
    return NULL;
}

// ===== EXTENDER =====
int try_extend_lv_home() {
    printf("\n[EXTENDER] 3-Step Auto-Extension Initiated...\n");

    // STEP 1: Check VG free
    FILE *fp = popen("vgs --noheadings -o vg_free --units g --nosuffix vgdata 2>/dev/null | awk '{print int($1)}'", "r");
    int free_gb = 0;
    if (fp) {
        if (fscanf(fp, "%d", &free_gb) != 1) free_gb = 0;
        pclose(fp);
    }
    printf("[STEP 1] VG free: %d GiB\n", free_gb);
    if (free_gb >= 1) {
        printf("[STEP 1] ✅ Extending using VG free space\n");
        return run_cmd("lvextend -r -L +1G /dev/vgdata/lv_home");
    }

    // STEP 2: Donor search
    const char *donors[][2] = {
        {"/mnt/lv_data1", "lv_data1"},
        {"/mnt/lv_data2", "lv_data2"},
        {NULL, NULL}
    };

    printf("[STEP 2] Searching donors...\n");
    for (int i = 0; donors[i][0]; i++) {
        const char *mnt = donors[i][0];
        const char *lv = donors[i][1];
        int pct = get_usage_pct(mnt);
        if (pct < 0 || pct >= LOW_PCT) continue;

        struct statvfs buf;
        if (statvfs(mnt, &buf) != 0) continue;
        long long fs_free_gb = (buf.f_bfree * (long long)buf.f_frsize) / (1024LL * 1024 * 1024);
        if (fs_free_gb < 1) continue;

        printf("[STEP 2] 🎯 Donor %s (%d%% used, %lld GiB free FS)\n", lv, pct, fs_free_gb);
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "lvreduce -r -L -1G /dev/vgdata/%s -y", lv);
        if (run_cmd(cmd) == 0) {
            printf("[STEP 2] → Extending lv_home...\n");
            return run_cmd("lvextend -r -L +1G /dev/vgdata/lv_home");
        }
    }

    // STEP 3: Fallback disk
    printf("[STEP 3] Adding fallback PV %s...\n", FALLBACK_DEV);
    run_cmd("pvcreate -y /dev/sdc");
    run_cmd("vgextend vgdata /dev/sdc");
    return run_cmd("lvextend -r -L +1G /dev/vgdata/lv_home");
}

// ===== SUPERVISOR THREAD =====
void *supervisor_thread(void *arg) {
    printf("Supervisor active (threshold=%d%%)\n", THRESHOLD_PCT);
    while (1) {
        int pct = get_usage_pct(WRITER_BASE_PATH);
        if (pct >= THRESHOLD_PCT) {
            printf("\n⚠️  HUNGRY: %s at %d%%\n", WRITER_BASE_PATH, pct);
            try_extend_lv_home();
        }
        sleep(5);
    }
    return NULL;
}

// ===== MAIN =====
int main() {
    printf("\n🚀 LVM Auto-Extender Lab\n");
    printf("   DRY_RUN=%s | Threshold=%d%% | Fallback=%s\n\n", 
           DRY_RUN ? "YES" : "NO", THRESHOLD_PCT, FALLBACK_DEV);

    mkdir("/mnt/lv_home/writer", 0755);

    pthread_t writer_t, supervisor_t;
    pthread_create(&writer_t, NULL, writer_thread, NULL);
    pthread_create(&supervisor_t, NULL, supervisor_thread, NULL);

    printf("✅ Manager running. Ctrl+C to stop.\n\n");
    pthread_join(writer_t, NULL);
    return 0;
}