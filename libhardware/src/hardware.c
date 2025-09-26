// hardware.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <libgen.h>
#include <limits.h>
#include <time.h>          
#include <cjson/cJSON.h>

static pid_t g_rec_pid = -1;
static char  g_rec_target[PATH_MAX] = {0};

static int   g_rec_w = 1280, g_rec_h = 720, g_rec_fps = 30, g_rec_bitrate = 4000000;
static char  g_rec_device[64] = "/dev/video2";
static char  g_rec_dir[PATH_MAX] = "/data/records";

static void load_config_record(void) {
    const char *path = "/etc/aiblackbox/config.json";
    FILE *fp = fopen(path, "rb"); if (!fp) return;
    fseek(fp, 0, SEEK_END); long sz = ftell(fp); rewind(fp);
    char *buf = (char*)malloc(sz+1); if(!buf){ fclose(fp); return; }
    (void)fread(buf, 1, sz, fp);   // 경고 억제
    buf[sz] = '\0';
    fclose(fp);
    cJSON *root = cJSON_Parse(buf);
    if(root){
        cJSON *rec = cJSON_GetObjectItemCaseSensitive(root, "record");
        if(cJSON_IsObject(rec)){
            cJSON *j;
            if((j=cJSON_GetObjectItemCaseSensitive(rec,"device")) && cJSON_IsString(j)) strncpy(g_rec_device,j->valuestring,sizeof(g_rec_device)-1);
            if((j=cJSON_GetObjectItemCaseSensitive(rec,"width"))  && cJSON_IsNumber(j)) g_rec_w = j->valueint;
            if((j=cJSON_GetObjectItemCaseSensitive(rec,"height")) && cJSON_IsNumber(j)) g_rec_h = j->valueint;
            if((j=cJSON_GetObjectItemCaseSensitive(rec,"fps"))    && cJSON_IsNumber(j)) g_rec_fps = j->valueint;
            if((j=cJSON_GetObjectItemCaseSensitive(rec,"bitrate"))&& cJSON_IsNumber(j)) g_rec_bitrate = j->valueint;
            if((j=cJSON_GetObjectItemCaseSensitive(rec,"dir"))    && cJSON_IsString(j)) strncpy(g_rec_dir,j->valuestring,sizeof(g_rec_dir)-1);
        }
        cJSON_Delete(root);
    }
    free(buf);
}

static int ensure_parent_dir(const char *path){
    char tmp[PATH_MAX]; strncpy(tmp, path, sizeof(tmp)-1); tmp[sizeof(tmp)-1]='\0';
    char *d = dirname(tmp);
    return (d && (mkdir(d, 0775)==0 || access(d, W_OK)==0)) ? 0 : 0; // best-effort
}

