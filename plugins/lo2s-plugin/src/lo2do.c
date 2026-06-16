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

SPANK_PLUGIN(lo2do, 1);

static uint8_t lo2do_is_set = 0;
static char lo2s_trace_path[256] = ""; 

struct sigaction sa;

int _lo2do_cb(int val, const char *optarg, int remote) {
    
    if (remote) {
        lo2do_is_set = 1;
        if (optarg && strcmp(optarg, "(null)") != 0) {
            snprintf(lo2s_trace_path, sizeof(lo2s_trace_path), "%s", optarg);
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
        "lo2do_cgroup_path", "MODE_OPT", 
        "Mode is either \"attach\" or \"system_monitor\"", 
        1, 0, NULL
    },
    SPANK_OPTIONS_TABLE_END
};

int init_lo2s_factory(uint32_t job_id);
int close_lo2s_factory(uint32_t job_id);
int init_monitoring_process(uint32_t job_id, const char *trace_path, const char *cgroup_path);

void write_log(const char *log_str) {
    FILE *log_file = fopen("/tmp/spank_prolog.log", "a");
    if (log_file != NULL) {
        fprintf(log_file, "%s", log_str);
        fclose(log_file);
    }
}

int slurm_spank_init(spank_t sp, int ac, char **av) {
    spank_option_register(sp, &all_spank_options[0]);
    spank_option_register(sp, &all_spank_options[1]);
    return 0;
}

int slurm_spank_job_prolog(spank_t sp, int ac, char **av) {
    uint32_t job_id = 0; // STRENGER TYP: uint32_t
    
    if (spank_get_item(sp, S_JOB_ID, &job_id) != ESPANK_SUCCESS) {
        write_log("[Prolog] Fehler beim Holen der Job-ID\n");
        return 0;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN; 
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    int ret = init_lo2s_factory(job_id);
    write_log("after init factory: ");
    char *ret_is_0 = ret == 0 ? "is_zero\n" : "is not zero\n";
    write_log(ret_is_0);
    return ret;
}

int slurm_spank_init_post_opt(spank_t sp, int ac, char **av) {
    // 1. Kontext prüfen
    if (spank_context() != S_CTX_REMOTE) {
        return 0;
    }
    
    write_log("[SPANK] init_post_opt gestartet!\n");


    uint32_t job_id = 0;
    uint32_t step_id = 0; // Verwende uint32_t analog zu Slurm
    int node_id = 0;

    spank_err_t j_err = spank_get_item(sp, S_JOB_ID, &job_id);
    spank_err_t s_err = spank_get_item(sp, S_JOB_STEPID, &step_id);
    spank_err_t n_err = spank_get_item(sp, S_JOB_NODEID, &node_id);

    char log[256];
    snprintf(log, sizeof(log), "[SPANK] Kontext Remote -> JID: %u, SID: %u, NID: %d\n", job_id, step_id, node_id);
    write_log(log);

    // HINWEIS: Wenn du den Batch-Step (Skript-Executor) ignorieren willst, 
    // der hat oft SLURM_BATCH_SCRIPT (0xffffffff bzw. 4294967295).
    // Wenn du normale srun-Schritte loggen willst (SID: 0, 1, 2...), lass den Filter weg oder logge ihn nur:
    if (step_id > 0) { 
        write_log("[SPANK] Ignoriere Batch-Script-Step, warte auf echten Task-Step.\n");
        return 0; 
    }

    if (strlen(lo2s_trace_path) == 0) {
        if (!getcwd(lo2s_trace_path, sizeof(lo2s_trace_path))) {
            return -1;
        }
    }

    char final_trace_path[512];
    char job_cgroup_path[512];

    // Node-ID für Dateinamen nutzen
    snprintf(final_trace_path, sizeof(final_trace_path), "%s/lo2s_trace_%u_%d", 
             lo2s_trace_path, job_id, node_id);
    
    snprintf(job_cgroup_path, sizeof(job_cgroup_path), "/sys/fs/cgroup/system.slice/slurmstepd.scope/job_%u", job_id);
    
    if (access(job_cgroup_path, F_OK) != 0) {
        write_log("[CGROUP] FALLBACK auf system.slice\n");
        snprintf(job_cgroup_path, sizeof(job_cgroup_path), "/sys/fs/cgroup/system.slice/job_%u", job_id);
        if (access(job_cgroup_path, F_OK) != 0) {
            write_log("[CGROUP] FAIL: Cgroup-Pfad existiert nicht!\n");
        }
    }

    snprintf(log, sizeof(log), "[SPANK] Sende an Factory -> TP: %s | CP: %s\n", final_trace_path, job_cgroup_path);
    write_log(log);

    // Hier wird die Factory getriggert (argc == 4-Zweig)
    return init_monitoring_process(job_id, final_trace_path, job_cgroup_path);
}

int slurm_spank_job_epilog(spank_t sp, int ac, char **av) {
    uint32_t job_id = 0; // STRENGER TYP
    if (spank_get_item(sp, S_JOB_ID, &job_id) != ESPANK_SUCCESS) {
        return 0;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN; 
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    return close_lo2s_factory(job_id);
}








int init_lo2s_factory(uint32_t job_id) {
    pid_t pid1 = fork();
    if (pid1 < 0) return -1;

    if (pid1 == 0) { 
        pid_t pid2 = fork();
        if (pid2 < 0) exit(1);
        if (pid2 > 0) exit(0); // Vater stirbt sofort

        if (setsid() < 0) exit(1); // Eigene Session aufmachen

        // RADIKALE ENTKOPPLUNG: Unbedingt einkommentieren!
        freopen("/dev/null", "r", stdin);
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        
        char job_id_str[32];
        snprintf(job_id_str, sizeof(job_id_str), "%u", job_id);
        
        char *args[] = {"/usr/local/bin/lo2s_factory", job_id_str, NULL};
        execvp(args[0], args);
        
        exit(1);
    } else { 
        int status;
        waitpid(pid1, &status, 0); // Wartet nur auf das sofort sterbende erste Kind
    }
    return 0;
}


static int _execute_factory_cmd(uint32_t job_id, char *args[]) {
    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) { 
        freopen("/dev/null", "r", stdin);
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        
        write_log("INIT EXEC\n");
        execvp(args[0], args);
        
        write_log("EXEC FAILED\n");
        exit(1); 
    } else {
        int status;
        // Die 0 zwingt den Elternprozess (SPANK), auf das Kind zu warten.
        // Das schützt den Schreibvorgang in die Pipe vor dem Slurm-Cleanup!
        waitpid(pid, &status, 0); 
    }
    return 0;
}
int close_lo2s_factory(uint32_t job_id) {
    char job_id_str[32];
    snprintf(job_id_str, sizeof(job_id_str), "%u", job_id);

    char *args[] = {
        "/usr/local/bin/lo2s_factory", 
        job_id_str, 
        "exit_lo2s", 
        NULL
    };
    write_log("End Monitoring\n");

    return _execute_factory_cmd(job_id, args);
}

int init_monitoring_process(uint32_t job_id, const char *trace_path, const char *cgroup_path) {
    char job_id_str[32];
    snprintf(job_id_str, sizeof(job_id_str), "%u", job_id);

    char *args[] = {
        "/usr/local/bin/lo2s_factory", 
        job_id_str, 
        (char *)trace_path, 
        (char *)cgroup_path, 
        NULL
    };

    write_log("init_lo2s_factory\n");

    return _execute_factory_cmd(job_id, args);
}