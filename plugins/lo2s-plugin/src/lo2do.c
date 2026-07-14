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

#define FIFO_FILE "/tmp/factory_pipe"
#define FIFO_RETRY_COUNT 20
#define FIFO_RETRY_US 100000
#define MAX_FACTORY_COMMAND_LEN 1536
#define TRACE_PATH "/data"
#define TRACE_PATH_EDITABLE 1


SPANK_PLUGIN(lo2do, 1);

static uint8_t lo2do_is_set = 0;
static uint32_t sample_rate = 0;
static char lo2s_trace_path[256] = "";
static char lo2s_cgroup_path[256] = "";
static char lo2s_additional_args[256] = "";



int _lo2do_cb(int val, const char *optarg, int remote) {
    if (remote) {
        lo2do_is_set = 1;
        if (TRACE_PATH_EDITABLE && optarg && strcmp(optarg, "(null)") != 0) {
            snprintf(lo2s_trace_path, sizeof(lo2s_trace_path), "%s", optarg);
        }else{
            snprintf(lo2s_trace_path, sizeof(lo2s_trace_path), "%s", TRACE_PATH);
        }
    }
    return 0;
}

int _lo2do_args_cb(int val, const char *optarg, int remote) {
    if (remote) {
        nprintf(lo2s_additional_args, sizeof(lo2s_additional_args), "%s", optarg);

    }
    return 0;
}

struct spank_option all_spank_options[] = {
    {
        "lo2do", "TRACE_PATH", 
        "Enable lo2s System-Monitoring for given job per node, writing generated otf2-traces to given TRACE_PATH",
        TRACE_PATH_EDITABLE ? 2 : 0, 0, _lo2do_cb
    },
    {
        "lo2do_args", "ARGUMENTS",
        "Pass additional arguments to the lo2s monitoring process",
        1, 0, _lo2do_args_cb
    },
    SPANK_OPTIONS_TABLE_END
};

int init_lo2d(uint32_t job_id);
int close_lo2sd(uint32_t job_id);
int init_monitoring_process(uint32_t job_id, const char *trace_path, const char *cgroup_path, const char *additional_args);
static int send_factory_command(uint32_t job_id, const char *payload);
static int send_factory_exit(uint32_t job_id);

void write_log(const char *log_str) {
    FILE *log_file = fopen("/tmp/spank_prolog.log", "a");
    if (log_file != NULL) {
        fprintf(log_file, "%s", log_str);
        fflush(log_file);
        fclose(log_file);
    }
}

void write_logf(const char *format, ...) {
    FILE *log_file = fopen("/tmp/spank_prolog.log", "a");
    if (log_file == NULL) return;
    va_list ap;
    va_start(ap, format);
    vfprintf(log_file, format, ap);
    va_end(ap);
    fflush(log_file);
    fclose(log_file);
}

int slurm_spank_init(spank_t sp, int ac, char **av) {
    spank_option_register(sp, &all_spank_options[0]);
    spank_option_register(sp, &all_spank_options[1]);
    return 0;
}


int slurm_spank_job_prolog(spank_t sp, int ac, char **av) {
    uint32_t job_id = 0;
    if (spank_get_item(sp, S_JOB_ID, &job_id) != ESPANK_SUCCESS) return 1;
    return init_lo2d(job_id);
;
}


int slurm_spank_init_post_opt(spank_t sp, int ac, char **av) {
    if (spank_context() != S_CTX_REMOTE) {
        return 0;
    }
    if (!lo2do_is_set) {
        write_log("[SPANK] lo2do nicht aktiviert, überspringe Task-Post-Fork.\n");
        // TODO: close daemon

        return 0;
    }
    
    uint32_t job_id = 0;
    uint32_t step_id = 0; 
    int node_id = 0;
    spank_get_item(sp, S_JOB_NODEID, &node_id);
    spank_get_item(sp, S_JOB_ID, &job_id);
    spank_get_item(sp, S_JOB_STEPID, &step_id);
 

    if (step_id != 0)return 0; 
    
    char final_trace_path[512];
    char job_cgroup_path[512];
    snprintf(final_trace_path, sizeof(final_trace_path), "%s/lo2s_trace_%u_%d", lo2s_trace_path, job_id, node_id);
    snprintf(job_cgroup_path, sizeof(job_cgroup_path), "/sys/fs/cgroup/system.slice/job_%u", job_id);

    
    const char *additional_args = strlen(lo2s_additional_args) ? lo2s_additional_args : NULL;
    return init_monitoring_process(job_id, final_trace_path, job_cgroup_path, additional_args);
}


int slurm_spank_job_epilog(spank_t sp, int ac, char **av) {
    uint32_t job_id = 0;
    if (spank_get_item(sp, S_JOB_ID, &job_id) != ESPANK_SUCCESS) return 1;
    int ret = close_lo2sd(job_id);
    return ret;
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
        
        char *args[] = {"/usr/local/bin/lo2d", job_id_str, NULL};
        execvp(args[0], args);
        exit(1);
    } else { 
        int status;
        waitpid(pid1, &status, 0); 
    }
    return 0;
}


static int send_factory_command(uint32_t job_id, const char *payload) {
    char pipe_path[128];
    snprintf(pipe_path, sizeof(pipe_path), "%s%u", FIFO_FILE, job_id);

    int fifo_fd = -1;
    for (int i = 0; i < FIFO_RETRY_COUNT; ++i) {
        fifo_fd = open(pipe_path, O_WRONLY | O_CLOEXEC);
        if (fifo_fd >= 0) {
            break;
        }
        if (errno == ENXIO || errno == ENOENT) {
            usleep(FIFO_RETRY_US);
            continue;
        }
        write_logf("[SPANK][CMD] open(%s) failed: %s\n", pipe_path, strerror(errno));
        return -1;
    }
    if (fifo_fd < 0) {
        write_logf("[SPANK][CMD] open(%s) failed after retries: %s\n", pipe_path, strerror(errno));
        return -1;
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

static int send_factory_exit(uint32_t job_id) {
    return send_factory_command(job_id, "exit_lo2s");
}

int init_monitoring_process(uint32_t job_id, const char *trace_path, const char *cgroup_path, const char *additional_args) {
    char payload[MAX_FACTORY_COMMAND_LEN];
    if (additional_args && additional_args[0] != '\0') {
        snprintf(payload, sizeof(payload), "%s;%s;%s", trace_path, cgroup_path, additional_args);
    } else {
        snprintf(payload, sizeof(payload), "%s;%s", trace_path, cgroup_path);
    }
    return send_factory_command(job_id, payload);
}

int close_lo2sd(uint32_t job_id) {
    return send_factory_exit(job_id);
}

