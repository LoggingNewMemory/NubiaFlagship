#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <errno.h>

#define PREFS_FILE "/data/data/com.zte.fingerprints/shared_prefs/com.zte.fingerprints_preferences.xml"
#define SAVED_FILE "/data/adb/modules/NubiaNeo3GT5GFix/Saved.txt"
#define EVENT_SIZE  ( sizeof (struct inotify_event) )
#define BUF_LEN     ( 1024 * ( EVENT_SIZE + 16 ) )

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);
    if (setsid() < 0) exit(EXIT_FAILURE);
    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);
    umask(0);
}

void get_current_theme(char *theme_out) {
    FILE *f = fopen(PREFS_FILE, "r");
    theme_out[0] = '\0';
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *ptr = strstr(line, "default_theme_");
        if (ptr) {
            char *end = strstr(ptr, "<");
            if (end) {
                int len = end - ptr;
                if (len < 64) {
                    strncpy(theme_out, ptr, len);
                    theme_out[len] = '\0';
                }
                break;
            }
        }
    }
    fclose(f);
}

void get_current_anim_style(char *anim_out) {
    anim_out[0] = '\0';
    FILE *f = popen("settings get global unlock_sensorui_animal_style", "r");
    if (f) {
        if (fgets(anim_out, 32, f)) {
            anim_out[strcspn(anim_out, "\r\n")] = 0;
        }
        pclose(f);
    }
}

void get_saved_state(char *theme_out, char *anim_out) {
    theme_out[0] = '\0';
    anim_out[0] = '\0';
    FILE *f = fopen(SAVED_FILE, "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = 0;
            if (strncmp(line, "theme_ID=", 9) == 0) {
                strcpy(theme_out, line + 9);
            } else if (strncmp(line, "anim_style=", 11) == 0) {
                strcpy(anim_out, line + 11);
            }
        }
        fclose(f);
    }
}

void save_state(const char *theme) {
    char anim_style[32] = {0};
    get_current_anim_style(anim_style);
    
    if (strlen(anim_style) == 0) {
        strcpy(anim_style, "9"); // Fallback
    }
    
    FILE *f = fopen(SAVED_FILE, "w");
    if (f) {
        fprintf(f, "theme_ID=%s\n", theme);
        fprintf(f, "anim_style=%s\n", anim_style);
        fclose(f);
    }
}

void enforce_state() {
    char current_theme[64] = {0};
    get_current_theme(current_theme);
    
    if (strlen(current_theme) == 0) return;

    char saved_theme[64] = {0};
    char saved_anim[32] = {0};
    get_saved_state(saved_theme, saved_anim);

    if (strlen(saved_theme) == 0) {
        // First run or file missing, assume current is what they want
        if (strcmp(current_theme, "default_theme_1") != 0 && strcmp(current_theme, "default_theme_0") != 0) {
            save_state(current_theme);
        } else {
            save_state("default_theme_27"); 
        }
        return;
    }

    if (strcmp(current_theme, "default_theme_1") == 0 || strcmp(current_theme, "default_theme_0") == 0) {
        // System reverted it! Restore the saved state.
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "sed -i 's/%s/%s/g' %s", current_theme, saved_theme, PREFS_FILE);
        system(cmd);
        
        system("chown system:system " PREFS_FILE);
        system("chmod 660 " PREFS_FILE);
        
        if (strlen(saved_anim) > 0) {
            snprintf(cmd, sizeof(cmd), "settings put global unlock_sensorui_animal_style %s", saved_anim);
            system(cmd);
        }
    } else if (strcmp(current_theme, saved_theme) != 0) {
        // Theme was intentionally changed to something else by user! Wait 2 seconds for settings to flush, then save it.
        sleep(2);
        save_state(current_theme);
    } else {
        // Matches saved theme, but system might have reset the global setting without touching XML on boot!
        if (strlen(saved_anim) > 0) {
            char cmd[512];
            snprintf(cmd, sizeof(cmd), "settings put global unlock_sensorui_animal_style %s", saved_anim);
            system(cmd);
        }
    }
}

int main() {
    daemonize();
    prctl(PR_SET_NAME, "FODAnimFix");

    // Check and enforce theme on initial run (boot)
    enforce_state();

    int fd = inotify_init();
    if (fd < 0) {
        return 1;
    }

    int wd = inotify_add_watch(fd, "/data/data/com.zte.fingerprints/shared_prefs/", IN_CLOSE_WRITE | IN_MOVED_TO);
    if (wd < 0) {
        return 1;
    }

    char buffer[BUF_LEN];
    while (1) {
        int length = read(fd, buffer, BUF_LEN);
        if (length < 0) {
            if (errno == EINTR) continue;
            break;
        }

        int i = 0;
        while (i < length) {
            struct inotify_event *event = (struct inotify_event *) &buffer[i];
            if (event->len) {
                if (strcmp(event->name, "com.zte.fingerprints_preferences.xml") == 0) {
                    enforce_state();
                }
            }
            i += EVENT_SIZE + event->len;
        }
    }
    inotify_rm_watch(fd, wd);
    close(fd);
    return 0;
}
