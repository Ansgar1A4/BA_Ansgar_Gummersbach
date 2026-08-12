#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#include <stddef.h>
#include <string.h>
#include <slurm/spank.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h> 
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>
#include <ftw.h>
#include <limits.h>
#include <time.h>
#include <stdbool.h>


#define FIFO_RETRY_COUNT 20
#define FIFO_RETRY_US 100000
#define MAX_FACTORY_COMMAND_LEN 1536

// TODO: Configurable Arguments
#define DEFAULT_TRACE_PATH "/data"              //required
#define DAEMON_PATH "/usr/local/bin/lo2d"       //required
#define TRACE_PATH_EDITABLE true                //optional DEFAULT: false
#define FIFO_FILE "/tmp/lo2d_pipe"              //optional DEFAULT: /tmp    TODO: just path

// Name und Version des Plugins
SPANK_PLUGIN(lo2do, 1);

static uint8_t lo2do_is_set = 0;
static char lo2s_trace_path[256] = "";
static char lo2s_cgroup_path[256] = "";
static char lo2s_additional_args[256] = "";

static char lo2s_info_text[256] = "";
uid_t target_uid = 0; 
gid_t target_gid = 0;


// Helpfunction to debug plugin functinality
void write_logf(const char *format, ...) {
    FILE *log_file = fopen("/tmp/spank_lo2do.log", "a");
    if (log_file == NULL) return;
    va_list ap;
    va_start(ap, format);
    vfprintf(log_file, format, ap);
    va_end(ap);
    fflush(log_file);
    fclose(log_file);
}


// CALLBACKS:

// callback executed when the lo2do option is set
int _lo2do_cb(int val, const char *optarg, int remote) {
    if (remote) {
        lo2do_is_set = 1;
        if (TRACE_PATH_EDITABLE && optarg && strcmp(optarg, "(null)") != 0) {
            snprintf(lo2s_trace_path, sizeof(lo2s_trace_path), "%s", optarg);
        }else{
            snprintf(lo2s_trace_path, sizeof(lo2s_trace_path), "%s", DEFAULT_TRACE_PATH);
        }
    }
    return 0;
}
// callback executed when the lo2do_args option is set
int _lo2do_args_cb(int val, const char *optarg, int remote) {
    if (remote) {
        snprintf(lo2s_additional_args, sizeof(lo2s_additional_args), "%s", optarg);
    }
    return 0;
}

// OPTION TABLE:

struct spank_option all_spank_options[] = {
    {
        "lo2do", 
        TRACE_PATH_EDITABLE ? "TRACE_PATH": NULL, 
        lo2s_info_text,
        TRACE_PATH_EDITABLE ? 2 : 0, 0, _lo2do_cb
    },
    {
        "lo2do_args", 
        "ARGUMENTS",
        "Pass additional arguments to the lo2s monitoring process, find optional arguments in the lo2s documentation, man-page or at: https://github.com/tud-zih-energy/lo2s/blob/master/man/lo2s.1.pod",
        1, 0, _lo2do_args_cb
    },
    SPANK_OPTIONS_TABLE_END
};

// HELPER FUNCTIONS:

// Declaring helpfunctions, see defintions below
int init_lo2d(uint32_t job_id);
int close_lo2sd(uint32_t job_id);
int init_monitoring_process(uint32_t job_id, const char *trace_path, const char *cgroup_path, const char *additional_args);

// SPANK HOOKS

// Hook used to register the plugin-options
int slurm_spank_init(spank_t sp, int ac, char **av) {
    // Set Information text for the lo2do option based on whether TRACE_PATH is editable or not
    if (TRACE_PATH_EDITABLE) {
        snprintf(lo2s_info_text, sizeof(lo2s_info_text), 
        "lo2do: Enable lo2s System-Monitoring for given job per node, writing generated lo2s-trace-direcotries to given TRACE_PATH, or to %s if TRACE_PATH is not specified.", 
        DEFAULT_TRACE_PATH);
    } else {
        snprintf(lo2s_info_text, sizeof(lo2s_info_text), 
        "lo2do: Enable lo2s System-Monitoring for given job per node, writing generated lo2s-trace-direcotries to %s.", 
        DEFAULT_TRACE_PATH);
    }
    // Register the options with SPANK in REMOTE and ALLOCATOR contexts
    spank_option_register(sp, &all_spank_options[0]);
    spank_option_register(sp, &all_spank_options[1]);
    return 0;
}

// Hook used to start the lo2s daemon for the job, which will listen for commands from the spank plugin
int slurm_spank_job_prolog(spank_t sp, int ac, char **av) {
    uint32_t job_id = 0;
    if (spank_get_item(sp, S_JOB_ID, &job_id) != ESPANK_SUCCESS) return 1;
    write_logf("[SPANK] Starting lo2s daemon for job %u, user %u\n", job_id, getuid());
    return init_lo2d(job_id);
}

// Hook used to start the lo2s monitoring process for the job, which will be started right after the cgroups had been set up by slurm
int slurm_spank_init_post_opt(spank_t sp, int ac, char **av) {
    // Ensuring that this hook is only executed in the REMOTE context
    if (spank_context() != S_CTX_REMOTE) {
        write_logf("[SPANK] slurm_spank_init_post_opt called in context: %d, skipping lo2s monitoring process initialization.\n", spank_context());
        return 0;
    }
    
    uint32_t step_id = 0; 
    spank_get_item(sp, S_JOB_STEPID, &step_id);
 
    // Only execute the monitoring process initialization for the first step of the job
    // MIND: If this functionality would be used after fork, ensure that the monitoring is just started for the first task. (e.g.: slurm_spank_task_init_privileged)
    // BUG?: Was wenn der erste Step eine kleinere Anzahl an Knoten nutzt?
    if (step_id != 0) return 0;

    
    uint32_t job_id = 0;
    spank_get_item(sp, S_JOB_ID, &job_id);

    
    // Check if the lo2do option was set, if not, skip the monitoring process initialization and close the lo2s daemon
    if (!lo2do_is_set) {
        write_logf("[SPANK] lo2do nicht aktiviert, überspringe Task-Post-Fork.\n");
        close_lo2sd(job_id);
        return 0;
    }

    uint32_t node_id = 0;
    uint32_t node_count = 0;

    spank_get_item(sp, S_JOB_NODEID, &node_id);
    spank_get_item(sp, S_JOB_NNODES, &node_count);

    char final_trace_path[512];
    snprintf(final_trace_path, sizeof(final_trace_path), "%s/lo2s_trace_%u_%d", lo2s_trace_path, job_id, node_id);

    char ugid_file[256] = "";
    snprintf(ugid_file, sizeof(ugid_file), "/tmp/lo2s_ugid_%d", job_id);
    FILE *f = fopen(ugid_file, "w");
    if (f) { fprintf(f, "%s", final_trace_path); fclose(f); }

    // cgroup_path is not used anymore, as we search for the cgroup path in the lo2s-helper process
    // TODO: Cleanup cgroup_path usage in the future, as it is not needed anymore
    char job_cgroup_path[512];
    snprintf(job_cgroup_path, sizeof(job_cgroup_path), "/sys/fs/cgroup/system.slice/job_%u", job_id);

    
    const char *additional_args = strlen(lo2s_additional_args) ? lo2s_additional_args : NULL;
    return init_monitoring_process(job_id, final_trace_path, job_cgroup_path, additional_args);
}



int change_owner_callback(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    (void)sb;
    (void)ftwbuf;

    if (lchown(fpath, target_uid, target_gid) != 0) {
        write_logf("[SPANK] Failed lchown on %s: %s\n", fpath, strerror(errno));
        return 0;
    }

    if (typeflag != FTW_SL && typeflag != FTW_SLN) {
        if (chmod(fpath, 0700) != 0) {
            write_logf("[SPANK] Failed chmod on %s: %s\n", fpath, strerror(errno));
            return 0;
        }
    }

    return 0;
}


int slurm_spank_job_epilog(spank_t sp, int ac, char **av) {
    uint32_t job_id = 0;
    if (spank_get_item(sp, S_JOB_ID, &job_id) != ESPANK_SUCCESS) return 1;
    if (close_lo2sd(job_id) == -2) return 0;
    usleep(10000000);
    char ugid_file[256] = "";
    snprintf(ugid_file, sizeof(ugid_file), "/tmp/lo2s_ugid_%d", job_id);

    char node_trace_path[256] = "";
    spank_get_item(sp, S_JOB_UID, &target_uid);
    spank_get_item(sp, S_JOB_GID, &target_gid);

    FILE *f = fopen(ugid_file, "r");
    if (f == NULL) {
        write_logf("[SPANK] Failed to open ugid_file %s for Job %u: %s\n", ugid_file, job_id, strerror(errno));
        return 1;
    }
    if (fscanf(f, "%255s", node_trace_path) != 1) {
        write_logf("[SPANK] Failed to read trace path from %s for Job %u\n", ugid_file, job_id);
        fclose(f);
        return 1;
    }
    fclose(f);

    // delete the ugid_file after reading it
    if (unlink(ugid_file) != 0) {
        write_logf("[SPANK] Failed to delete ugid_file for Job %u: %s\n", job_id, strerror(errno));
    }

    if (nftw(node_trace_path, change_owner_callback, 20, FTW_PHYS | FTW_DEPTH) != 0) {
        write_logf("[SPANK] nftw failed on %s for Job %u: %s\n", node_trace_path, job_id, strerror(errno));
    }
    return 0;
}


int init_lo2d(uint32_t job_id) {
    pid_t pid1 = fork();
    if (pid1 < 0) return -1;

    if (pid1 == 0) { 
        pid_t pid2 = fork();
        if (pid2 < 0) exit(1);
        if (pid2 > 0) exit(0);
        if (setsid() < 0) exit(1); 
        
        char log_path[256];
        snprintf(log_path, sizeof(log_path), "/tmp/lo2s_debug_%d.log", job_id);
        freopen(log_path, "a", stdout);
        freopen(log_path, "a", stderr);
                
        char job_id_str[32];
        snprintf(job_id_str, sizeof(job_id_str), "%u", job_id);
        
        char *args[] = {DAEMON_PATH, job_id_str, NULL};
        execvp(args[0], args);
        exit(1);
    } else { 
        int status;
        waitpid(pid1, &status, 0); 
    }
    // BUGFIX: Wait till lo2s started
    // TODO: Ask for a better solution, maybe a signal from lo2d to the spank plugin
    return 0;
}

// PIPE COMMUNICATION FUNCTIONS

static int _send_daemon_command(uint32_t job_id, const char *payload) {
    char pipe_path[128];
    snprintf(pipe_path, sizeof(pipe_path), "%s%u", FIFO_FILE, job_id);

    int fifo_fd = -1;
    for (int i = 0; i < FIFO_RETRY_COUNT; ++i) {
        fifo_fd = open(pipe_path, O_WRONLY | O_CLOEXEC);
        if (fifo_fd >= 0) {
            break;
        }
        if (errno == ENXIO || errno == ENOENT) {
            struct timespec retry_delay = {0, FIFO_RETRY_US * 1000};
            nanosleep(&retry_delay, NULL);
            continue;
        }
        write_logf("[SPANK][CMD] open(%s) failed: %s\n", pipe_path, strerror(errno));
        return -1;
    }
    // If we still couldn't open the FIFO after retries, log and return an error, implicating that the daemon might not be running or the FIFO is not available.
    if (fifo_fd < 0) {
        write_logf("[SPANK][CMD] open(%s) failed after retries: %s\n", pipe_path, strerror(errno));
        return -2;
    }

    ssize_t len = strlen(payload);
    ssize_t written = write(fifo_fd, payload, len);
    if (written != len) {
        write_logf("[SPANK][CMD] write(%s) failed: %s\n", pipe_path, strerror(errno));
        close(fifo_fd);
        return -1;
    }
    if (write(fifo_fd, "\n", 1) != 1) {
        write_logf("[SPANK][CMD] write newline to %s failed: %s\n", pipe_path, strerror(errno));
        close(fifo_fd);
        return -1;
    }
    close(fifo_fd);
    return 0;
}

int init_monitoring_process(uint32_t job_id, const char *trace_path, const char *cgroup_path, const char *additional_args) {
    char payload[MAX_FACTORY_COMMAND_LEN];
    if (additional_args && additional_args[0] != '\0') {
        snprintf(payload, sizeof(payload), "%s;%s;%s", trace_path, cgroup_path, additional_args);
    } else {
        snprintf(payload, sizeof(payload), "%s;%s", trace_path, cgroup_path);
    }
    int ret = _send_daemon_command(job_id, payload);
    sleep(2);
    return ret;
}

int close_lo2sd(uint32_t job_id) {
    return _send_daemon_command(job_id, "exit_lo2s");
}

