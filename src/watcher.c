#include <sys/inotify.h>
#include <limits.h>
#include <stdlib.h>
#include <syslog.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include "ini.h"
#include "curl/curl.h"


#define MAX_SUB_WDS 2000
#define DIR_NAME_MAX_LEN 100

#define MAX_RESPONSE_LEN 512

#define STATUS_NONE 0x00000000
#define STATUS_TXT 0x00000001
#define STATUS_MEDIA 0x00000002
#define STATUS_SUBTITLE 0x00000004

#define TIMEOUT_PERIOD 5
static void displayInotifyEvent1(struct inotify_event *i);
void fatal(const char *s) {
    syslog(LOG_LOCAL1 | LOG_CRIT, "%s : %s\n", s, strerror(errno));
    closelog();
    exit(-1);
}

int inotify_fd;
int wd;

typedef struct {
    int is_valid;
    int wd;
    long last_time;
    char dir_name[DIR_NAME_MAX_LEN];
    int status;
} subwatch_t;

typedef struct {
    CURL *curl;
    char *root_dir;
    char *adm_update_url;
    subwatch_t *subwatches;

    char response[MAX_RESPONSE_LEN];
    int  resp_len;
} app_data_t;

void update_via_curl(app_data_t *app_data, subwatch_t *psubwatch) {
    static CURLcode res;
    static char postfields[50];
    CURL *curl = app_data->curl;
    //bzero(postfields[50], sizeof(postfields));
    sprintf(postfields, "folderName=%s", psubwatch->dir_name);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postfields);

    app_data->resp_len = 0;

    res = curl_easy_perform(curl);
    if(res != CURLE_OK) {
        syslog(LOG_LOCAL1 | LOG_INFO, "Update (%s;%d): %s ", psubwatch->dir_name, psubwatch->status, curl_easy_strerror(res));
    }
    else {
        syslog(LOG_LOCAL1 | LOG_INFO, "Update (%s;%d): %.*s ", psubwatch->dir_name, psubwatch->status, app_data->resp_len, app_data->response);
    }
}

void *cleaner_proc(void *data) {
    int j = 0;
    long now = time(NULL);
    app_data_t *app_data = (app_data_t *) data;
    subwatch_t *subwatches = app_data->subwatches;
    syslog(LOG_LOCAL1 | LOG_DEBUG, "cleaner_proc start");
    while(1) {
        now = time(NULL);
        for(j = 0; j < MAX_SUB_WDS; j++) {
            if( subwatches[j].is_valid ) {
                if((now - subwatches[j].last_time > TIMEOUT_PERIOD) && 
                   (subwatches[j].status > STATUS_NONE) )
                {
                    update_via_curl(app_data, &(subwatches[j]));
                    subwatches[j].status = STATUS_NONE;
                }
            }
        }
        usleep(1000000);
    }
}

int config_parse_func(void *user, const char*section, const char *name, const char *value) {
    app_data_t *app_data = (app_data_t *)user;
    #define MATCH(n) ( strncmp(n, name, strlen(n)) == 0 )
    if( MATCH("vod_dir") ) {
        app_data->root_dir = strdup(value);
    }
    else if( MATCH("adm_update_url") ) {
        app_data->adm_update_url = strdup(value);
    }
    return 1;
}

void config_load(char *fname, app_data_t *app_data) {
    //app_data->root_dir = strdup("testdir");
    if(ini_parse(fname, config_parse_func, app_data)) {
        fatal("Error parsing config file");
    }
    if ( (app_data->root_dir == NULL) || (app_data->adm_update_url == NULL)) 
        fatal("Missing parameter in config file");
}

static void process_root_dir(struct inotify_event *i, subwatch_t *subwatches, app_data_t *app_data) {
    int j;
    static char subdir[2*DIR_NAME_MAX_LEN];
    displayInotifyEvent1(i);
    if ((i->mask & IN_CREATE) && (i->mask & IN_ISDIR)) {
        j = rand() % MAX_SUB_WDS;
        while(subwatches[j].is_valid) {
            j = rand() % MAX_SUB_WDS;
        }

        bzero(subdir, sizeof(subdir));
        sprintf(subdir, "%s/%s", app_data->root_dir, i->name);
        syslog(LOG_LOCAL1 | LOG_DEBUG, "add watch on %s (wd index: %d)", subdir, j);
        subwatches[j].wd = inotify_add_watch(inotify_fd, subdir, IN_ALL_EVENTS);
        if (subwatches[j].wd == -1)
            fatal("inotify_add_watch");
        
        subwatches[j].is_valid = 1;
        subwatches[j].last_time = time(NULL);
        bzero(subwatches[j].dir_name, sizeof(subwatches[j].dir_name));
        strncpy(subwatches[j].dir_name, i->name, strlen(i->name));
        subwatches[j].status = 0;
        syslog(LOG_LOCAL1 | LOG_DEBUG, "add watch on %s SUCCESS (wd index: %d)", i->name, j);
    }
}

/* Display information from inotify_event structure */
static void displayInotifyEvent1(struct inotify_event *i) {
    if (i->mask & IN_ACCESS)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_ACCESS on %s", i->name);
    if (i->mask & IN_ATTRIB)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_ATTRIB on %s", i->name);
    if (i->mask & IN_CLOSE_NOWRITE)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_CLOSE_NOWRITE on %s", i->name);
    if (i->mask & IN_CLOSE_WRITE)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_CLOSE_WRITE on %s", i->name);
    if (i->mask & IN_CREATE)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_CREATE on %s", i->name);
    if (i->mask & IN_DELETE)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_DELETE on %s", i->name);
    if (i->mask & IN_DELETE_SELF)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_DELETE_SELF on %s", i->name);
    if (i->mask & IN_IGNORED)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_IGNORED on %s", i->name);
    if (i->mask & IN_ISDIR)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_ISDIR on %s", i->name);
    if (i->mask & IN_MODIFY)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_MODIFY on %s", i->name);
    if (i->mask & IN_MOVE_SELF)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_MOVE_SELF on %s", i->name);
    if (i->mask & IN_MOVED_FROM)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_MOVED_FROM on %s", i->name);
    if (i->mask & IN_MOVED_TO)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_MOVED_TO on %s", i->name);
    if (i->mask & IN_OPEN)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_OPEN on %s", i->name);
    if (i->mask & IN_Q_OVERFLOW)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_Q_OVERFLOW on %s", i->name);
    if (i->mask & IN_UNMOUNT)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_UNMOUNT on %s", i->name);
}

static void displayInotifyEvent(struct inotify_event *i, subwatch_t *psubwatch) {
    if (i->mask & IN_ACCESS)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_ACCESS on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_ATTRIB)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_ATTRIB on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_CLOSE_NOWRITE)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_CLOSE_NOWRITE on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_CLOSE_WRITE)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_CLOSE_WRITE on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_CREATE)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_CREATE on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_DELETE)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_DELETE on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_DELETE_SELF)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_DELETE_SELF on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_IGNORED)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_IGNORED on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_ISDIR)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_ISDIR on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_MODIFY)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_MODIFY on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_MOVE_SELF)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_MOVE_SELF on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_MOVED_FROM)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_MOVED_FROM on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_MOVED_TO)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_MOVED_TO on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_OPEN)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_OPEN on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_Q_OVERFLOW)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_Q_OVERFLOW on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_UNMOUNT)
        syslog(LOG_LOCAL1 | LOG_DEBUG,"Event IN_UNMOUNT on %s in %s", i->name, psubwatch->dir_name);
}

static void process_sub_dir(struct inotify_event *i, subwatch_t *psubwatch) {
    char *point;
    displayInotifyEvent(i, psubwatch);
    psubwatch->last_time = time(NULL);
    if(i->mask & IN_CLOSE_WRITE) {
        point = strrchr(i->name, '.');
        if(point != NULL) {
            if(strcmp(point, ".txt") == 0) {
                psubwatch->status = psubwatch->status | STATUS_TXT;
            }
            else if(strcmp(point, ".mp4") == 0) {
                psubwatch->status = psubwatch->status | STATUS_MEDIA;
            }
            else if(strcmp(point, ".srt") == 0) {
                psubwatch->status = psubwatch->status | STATUS_SUBTITLE;
            }
        }
    }
}
static void dispatch(struct inotify_event *i, subwatch_t *subwatches, app_data_t *app_data) {
    int j;
    if (i->wd == wd) process_root_dir(i, subwatches, app_data);
    else {
        for( j = 0; j < MAX_SUB_WDS; j++ ) {
            if( subwatches[j].is_valid) {
                if( i->wd == subwatches[j].wd ){
                    process_sub_dir(i, &(subwatches[j]));
                    break;
                }
            }
        }
    }
}

static size_t app_save_response(char *response, size_t size, size_t nmemb, void *userdata) {
    app_data_t *app_data = (app_data_t *)userdata;
    size_t n = size * nmemb;
    if( n > (MAX_RESPONSE_LEN - app_data->resp_len) ) {
        n = (MAX_RESPONSE_LEN - app_data->resp_len);
    }
    if (n > 0) {
        memcpy(&(app_data->response[app_data->resp_len]), response, n);
        app_data->resp_len += n;
    }
    return n;
}

#define BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))

int main(int argc, char *argv[]) {
    app_data_t app_data;

    openlog("watcher", LOG_NDELAY | LOG_PID, LOG_LOCAL1);

    if( argc < 2 ) {
        fatal("Missing arguments");
    }
    config_load(argv[1], &app_data);
    srand(time(NULL));
    int j;

    app_data.curl = curl_easy_init();
    if(!app_data.curl) {
        fatal("Cannot init libcurl");
    }

    curl_easy_setopt(app_data.curl, CURLOPT_URL, app_data.adm_update_url);
    curl_easy_setopt(app_data.curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(app_data.curl, CURLOPT_WRITEFUNCTION, app_save_response);
    curl_easy_setopt(app_data.curl, CURLOPT_WRITEDATA, &app_data);

    subwatch_t subwatches[MAX_SUB_WDS];
    app_data.subwatches = subwatches;

    for( j = 0; j < MAX_SUB_WDS; j++ ) {
        subwatches[j].is_valid = 0;
        subwatches[j].status = STATUS_NONE;
    }

    //char buf[BUF_LEN] __attribute__ ((aligned(8)));
    char buf[BUF_LEN];
    ssize_t numRead;
    char *p;
    struct inotify_event *event;

    inotify_fd = inotify_init();
    if (inotify_fd == -1)
        fatal("inotify_init");

    //wd = inotify_add_watch(inotify_fd, app_data.root_dir, IN_CREATE);
    wd = inotify_add_watch(inotify_fd, app_data.root_dir, IN_ALL_EVENTS);
    if (wd == -1)
        fatal("inotify_add_watch");

    syslog(LOG_LOCAL1 | LOG_DEBUG,"Watching %s using wd %d\n", app_data.root_dir, wd);

    pthread_t cleaner_thread;
    if(pthread_create(&cleaner_thread, NULL, cleaner_proc, &app_data)) {
        fatal("pthread_create");
    }

    for (;;) {
        numRead = read(inotify_fd, buf, BUF_LEN);
        if (numRead == 0)
            fatal("read() from inotify fd returned 0!");

        if (numRead == -1)
            fatal("read");

        /* Process all of the events in buffer returned by read() */

        for (p = buf; p < buf + numRead; ) {
            event = (struct inotify_event *) p;
            dispatch(event, subwatches, &app_data);

            p += sizeof(struct inotify_event) + event->len;
        }
    }
    closelog();
    return 0;
}

