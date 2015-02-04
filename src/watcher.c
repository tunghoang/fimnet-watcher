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

#define MAX_SUB_WDS 100
#define DIR_NAME_MAX_LEN 100

#define STATUS_NONE 0x00000000
#define STATUS_TXT 0x00000001
#define STATUS_MEDIA 0x00000002
#define STATUS_SUBTITLE 0x00000004

#define TIMEOUT_PERIOD 10
void fatal(const char *s) {
    syslog(LOG_LOCAL1 | LOG_INFO, "%s : %s\n", s, strerror(errno));
    closelog();
    exit(-1);
}
void errExit(const char *s) {
    fatal(s);
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
    char *root_dir;
    subwatch_t *subwatches;
} app_data_t;

void update_via_curl(subwatch_t *psubwatch) {
    syslog(LOG_LOCAL1 | LOG_INFO, "Update via curl: %s status:%d", psubwatch->dir_name, psubwatch->status);
}

void *cleaner_proc(void *data) {
    int j = 0;
    long now = time(NULL);
    app_data_t *app_data = (app_data_t *) data;
    subwatch_t *subwatches = app_data->subwatches;
    syslog(LOG_LOCAL1 | LOG_INFO, "cleaner_proc start");
    while(1) {
        now = time(NULL);
        for(j = 0; j < MAX_SUB_WDS; j++) {
            if( subwatches[j].is_valid ) {
                if(now - subwatches[j].last_time > TIMEOUT_PERIOD) {
                    inotify_rm_watch(inotify_fd, subwatches[j].wd);
                    update_via_curl(&(subwatches[j]));
                    subwatches[j].is_valid = 0;
                    subwatches[j].status = STATUS_NONE;
                    syslog(LOG_LOCAL1 | LOG_INFO, "remove watch %s", subwatches[j].dir_name);
                }
            }
        }
        usleep(1000000);
    }
}

void load_config(app_data_t *app_data) {
    app_data->root_dir = strdup("testdir");
}


static void process_root_dir(struct inotify_event *i, subwatch_t *subwatches, app_data_t *app_data) {
    int j;
    static char subdir[2*DIR_NAME_MAX_LEN];
    if ((i->mask & IN_CREATE) && (i->mask & IN_ISDIR)) {
        j = rand() % MAX_SUB_WDS;
        while(subwatches[j].is_valid) {
            j = rand() % MAX_SUB_WDS;
        }

        bzero(subdir, sizeof(subdir));
        sprintf(subdir, "%s/%s", app_data->root_dir, i->name);
        syslog(LOG_LOCAL1 | LOG_INFO, "add watch on %s (wd index: %d)", subdir, j);
        subwatches[j].wd = inotify_add_watch(inotify_fd, subdir, IN_ALL_EVENTS);
        if (subwatches[j].wd == -1)
            errExit("inotify_add_watch");
        
        subwatches[j].is_valid = 1;
        subwatches[j].last_time = time(NULL);
        bzero(subwatches[j].dir_name, sizeof(subwatches[j].dir_name));
        strncpy(subwatches[j].dir_name, i->name, strlen(i->name));
        subwatches[j].status = 0;
        syslog(LOG_LOCAL1 | LOG_INFO, "add watch on %s SUCCESS (wd index: %d)", i->name, j);
    }
}

/* Display information from inotify_event structure */
static void displayInotifyEvent(struct inotify_event *i, subwatch_t *psubwatch) {
    if (i->mask & IN_ACCESS)
        syslog(LOG_LOCAL1 | LOG_INFO,"Event IN_ACCESS on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_ATTRIB)
        syslog(LOG_LOCAL1 | LOG_INFO,"Event IN_ATTRIB on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_CLOSE_NOWRITE)
        syslog(LOG_LOCAL1 | LOG_INFO,"Event IN_CLOSE_NOWRITE on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_CLOSE_WRITE)
        syslog(LOG_LOCAL1 | LOG_INFO,"Event IN_CLOSE_WRITE on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_CREATE)
        syslog(LOG_LOCAL1 | LOG_INFO,"Event IN_CREATE on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_DELETE)
        syslog(LOG_LOCAL1 | LOG_INFO,"Event IN_DELETE on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_DELETE_SELF)
        syslog(LOG_LOCAL1 | LOG_INFO,"Event IN_DELETE_SELF on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_IGNORED)
        syslog(LOG_LOCAL1 | LOG_INFO,"Event IN_IGNORED on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_ISDIR)
        syslog(LOG_LOCAL1 | LOG_INFO,"Event IN_ISDIR on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_MODIFY)
        syslog(LOG_LOCAL1 | LOG_INFO,"Event IN_MODIFY on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_MOVE_SELF)
        syslog(LOG_LOCAL1 | LOG_INFO,"Event IN_MOVE_SELF on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_MOVED_FROM)
        syslog(LOG_LOCAL1 | LOG_INFO,"Event IN_MOVED_FROM on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_MOVED_TO)
        syslog(LOG_LOCAL1 | LOG_INFO,"Event IN_MOVED_TO on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_OPEN)
        syslog(LOG_LOCAL1 | LOG_INFO,"Event IN_OPEN on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_Q_OVERFLOW)
        syslog(LOG_LOCAL1 | LOG_INFO,"Event IN_Q_OVERFLOW on %s in %s", i->name, psubwatch->dir_name);
    if (i->mask & IN_UNMOUNT)
        syslog(LOG_LOCAL1 | LOG_INFO,"Event IN_UNMOUNT on %s in %s", i->name, psubwatch->dir_name);
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

#define BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))

int main() {
    app_data_t app_data;
    load_config(&app_data);
    srand(time(NULL));
    int j;

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

    openlog("fimnet-inotify:", LOG_NDELAY | LOG_PID, LOG_LOCAL1);

    inotify_fd = inotify_init();
    if (inotify_fd == -1)
        errExit("inotify_init");

    wd = inotify_add_watch(inotify_fd, app_data.root_dir, IN_CREATE);
    if (wd == -1)
        errExit("inotify_add_watch");

    syslog(LOG_LOCAL1 | LOG_INFO,"Watching %s using wd %d\n", app_data.root_dir, wd);


    pthread_t cleaner_thread;
    if(pthread_create(&cleaner_thread, NULL, cleaner_proc, &app_data)) {
        errExit("pthread_create");
    }

    for (;;) {
        numRead = read(inotify_fd, buf, BUF_LEN);
        if (numRead == 0)
            fatal("read() from inotify fd returned 0!");

        if (numRead == -1)
            errExit("read");

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
