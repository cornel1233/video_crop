// batch_portrait_and_rotate.c
// Build:   cc -O2 -Wall -o batch_portrait_and_rotate batch_portrait_and_rotate.c
// Usage:   ./batch_portrait_and_rotate
// Requires: ffmpeg in PATH (ffmpeg -version)

#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdbool.h>
#include <errno.h>

static const char *PORTRAIT_DIR = "portrait_clips";
static const char *ROTATE_DIR   = "rotated_left";

// simple check for file extension (case-insensitive)
static bool has_ext(const char *name, const char *ext) {
    size_t ln = strlen(name), le = strlen(ext);
    if (ln < le + 1) return false;
    const char *dot = name + ln - le;
    // case-insensitive compare
    for (size_t i = 0; i < le; ++i) {
        char a = dot[i], b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    // ensure preceding char is a dot
    return (ln > le && name[ln - le - 1] == '.');
}

static bool is_video_file(const char *name) {
    return has_ext(name, "mp4") || has_ext(name, "mov") || has_ext(name, "mkv");
}

static int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return 0;
        fprintf(stderr, "Pfad existiert, ist aber kein Verzeichnis: %s\n", path);
        return -1;
    }
    if (mkdir(path, 0755) == 0) return 0;
    if (errno == EEXIST) return 0;
    perror("mkdir");
    return -1;
}

// remove extension, return newly allocated string (caller frees)
static char* basename_no_ext(const char *filename) {
    const char *slash = strrchr(filename, '/');
    const char *base = slash ? slash + 1 : filename;
    const char *dot = strrchr(base, '.');
    size_t len = dot ? (size_t)(dot - base) : strlen(base);
    char *out = (char*)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, base, len);
    out[len] = '\0';
    return out;
}

static void run_cmd_or_report(const char *cmd) {
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "FFmpeg-Befehl fehlgeschlagen (code %d):\n%s\n", ret, cmd);
    }
}

int main(void) {
    // Create output folders
    if (ensure_dir(PORTRAIT_DIR) != 0) return 1;
    if (ensure_dir(ROTATE_DIR)   != 0) return 1;

    DIR *d = opendir(".");
    if (!d) { perror("opendir"); return 1; }

    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (ent->d_type == DT_DIR) continue; // skip directories
        const char *name = ent->d_name;
        if (!is_video_file(name)) continue;

        char *base = basename_no_ext(name);
        if (!base) { fprintf(stderr, "OOM bei %s\n", name); continue; }

        // ---------- 3 Portrait-Crops (9:16) ----------
        //
        // out_w = floor(ih * 9/16), out_h = ih
        // left:  x = 0
        // mid:   x = (iw - out_w)/2
        // right: x = iw - out_w
        //
        // Hinweis: floor() sorgt für "gerade" Werte; Encoder mögen gerade Dimensionen.
        // Re-encode: libx264, Preset faster, CRF 18; Audio unverändert.
        const char *vf_left  = "crop=floor(ih*9/16):ih:0:0";
        const char *vf_mid   = "crop=floor(ih*9/16):ih:(iw-floor(ih*9/16))/2:0";
        const char *vf_right = "crop=floor(ih*9/16):ih:(iw-floor(ih*9/16)):0";

        char out_left[1024], out_mid[1024], out_right[1024];
        snprintf(out_left,  sizeof(out_left),  "%s/%s_left_9x16.mp4",  PORTRAIT_DIR, base);
        snprintf(out_mid,   sizeof(out_mid),   "%s/%s_mid_9x16.mp4",   PORTRAIT_DIR, base);
        snprintf(out_right, sizeof(out_right), "%s/%s_right_9x16.mp4", PORTRAIT_DIR, base);

        char cmd[4096];

        // LEFT
        snprintf(cmd, sizeof(cmd),
                 "ffmpeg -y -i \"%s\" -map 0:v:0 -map 0:a? "
                 "-vf \"%s\" -c:v libx264 -preset faster -crf 18 -c:a copy "
                 "-movflags +faststart \"%s\"",
                 name, vf_left, out_left);
        run_cmd_or_report(cmd);

        // MID
        snprintf(cmd, sizeof(cmd),
                 "ffmpeg -y -i \"%s\" -map 0:v:0 -map 0:a? "
                 "-vf \"%s\" -c:v libx264 -preset faster -crf 18 -c:a copy "
                 "-movflags +faststart \"%s\"",
                 name, vf_mid, out_mid);
        run_cmd_or_report(cmd);

        // RIGHT
        snprintf(cmd, sizeof(cmd),
                 "ffmpeg -y -i \"%s\" -map 0:v:0 -map 0:a? "
                 "-vf \"%s\" -c:v libx264 -preset faster -crf 18 -c:a copy "
                 "-movflags +faststart \"%s\"",
                 name, vf_right, out_right);
        run_cmd_or_report(cmd);

        // ---------- 90° links (CCW) gedreht, ohne Crop ----------
        // transpose=2 ist 90° gegen den Uhrzeigersinn.
        char out_rot[1024];
        snprintf(out_rot, sizeof(out_rot), "%s/%s_rotated_left_90.mp4", ROTATE_DIR, base);

        snprintf(cmd, sizeof(cmd),
                 "ffmpeg -y -i \"%s\" -map 0:v:0 -map 0:a? "
                 "-vf \"transpose=2\" -c:v libx264 -preset faster -crf 18 -c:a copy "
                 "-movflags +faststart \"%s\"",
                 name, out_rot);
        run_cmd_or_report(cmd);

        free(base);
    }

    closedir(d);
    printf("Fertig. Ergebnisse in '%s/' und '%s/'\n", PORTRAIT_DIR, ROTATE_DIR);
    return 0;
}
