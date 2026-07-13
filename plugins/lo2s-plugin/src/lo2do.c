#include <stddef.h>
#include <string.h>
#include <slurm/spank.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h> 
#include <stdint.h>
#include <stdarg.h>

SPANK_PLUGIN(lo2do, 1);

static uint8_t lo2do_is_set = 0;
static uint32_t sample_rate = 0;
static char lo2s_trace_path[256] = "";
static char lo2s_cgroup_path[256] = "";
static char lo2s_additional_args[256] = "";


int _lo2do_cb(int val, const char *optarg, int remote) {
    if (remote) {
        lo2do_is_set = 1;
        if (optarg && strcmp(optarg, "(null)") != 0) {
            snprintf(lo2s_trace_path, sizeof(lo2s_trace_path), "%s", optarg);
        }
    }
    return 0;
}

int _lo2do_args_cb(int val, const char *optarg, int remote) {
    if (remote) {
        if (optarg && strcmp(optarg, "(null)") != 0) {
            snprintf(lo2s_additional_args, sizeof(lo2s_additional_args), "%s", optarg);
        }
    }
    return 0;
}

struct spank_option all_spank_options[] = {
    {
        "lo2do", "TRACE_PATH", 
        "Enable lo2s System-Monitoring for given job per node, writing generated otf2-traces to given TRACE_PATH",
        2, 0, _lo2do_cb
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

    if (strlen(lo2s_trace_path) == 0) {
        if (!getcwd(lo2s_trace_path, sizeof(lo2s_trace_path))) {
            return -1;
        }
    }

    char final_trace_path[512];
    char job_cgroup_path[512];
    snprintf(final_trace_path, sizeof(final_trace_path), "%s/lo2s_trace_%u_%d", lo2s_trace_path, job_id, node_id);
    snprintf(job_cgroup_path, sizeof(job_cgroup_path), "/sys/fs/cgroup/system.slice/job_%u", job_id);

    if (access(job_cgroup_path, F_OK) != 0) {
        snprintf(job_cgroup_path, sizeof(job_cgroup_path), "/sys/fs/cgroup/cpu/slurm/uid_%u/job_%u", getuid(), job_id);
    }
    
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


/// @brief UNUSED: Führt den lo2d-Befehl aus, wartet auf dessen Beendigung und gibt den Status zurück.
/// @param job_id 
/// @param args 
/// @return 
static int _execute_factory_cmd(uint32_t job_id, char *args[]) {
    pid_t pid = fork();
    if (pid < 0) {
        write_logf("[SPANK][CMD] fork failed for job_id=%u: %s\n", job_id, strerror(errno));
        return -1;
    }

    if (pid == 0) {
        execvp(args[0], args);
        _exit(127);
    } else {
        int status;
        if (waitpid(pid, &status, 0) < 0) {
            write_logf("[SPANK][CMD] waitpid failed for job_id=%u: %s\n", job_id, strerror(errno));
            return -1;
        }
        if (WIFEXITED(status)) {
            int rc = WEXITSTATUS(status);
            if (rc != 0) {
                write_logf("[SPANK][CMD] command exited rc=%d for job_id=%u\n", rc, job_id);
            } else {
                write_logf("[SPANK][CMD] command exited success for job_id=%u\n", job_id);
            }
            return rc;
        }
        if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
            write_logf("[SPANK][CMD] command killed by signal %d for job_id=%u\n", sig, job_id);
            return 128 + sig;
        }
        write_logf("[SPANK][CMD] waitpid returned unknown status 0x%x for job_id=%u\n", status, job_id);
        return status;
    }
}

int init_monitoring_process(uint32_t job_id, const char *trace_path, const char *cgroup_path, const char *additional_args) {
    char job_id_str[32];
    snprintf(job_id_str, sizeof(job_id_str), "%u", job_id);
    char *args[] = {"/usr/local/bin/lo2d", job_id_str, (char *)trace_path, (char *)cgroup_path, (char *)additional_args, NULL};
    //write_log("init_lo2s_monitoring\n");
    return _execute_factory_cmd(job_id, args);
}

int close_lo2sd(uint32_t job_id) {
    char job_id_str[32];
    snprintf(job_id_str, sizeof(job_id_str), "%u", job_id);
    char *args[] = {"/usr/local/bin/lo2d", job_id_str, "exit_lo2s", NULL};
    return _execute_factory_cmd(job_id, args);
}

