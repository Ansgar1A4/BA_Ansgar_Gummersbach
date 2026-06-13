#include <stddef.h>
#include <string.h>
#include <slurm/spank.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h> // Hinzugefügt für waitpid
#include <stdint.h>

SPANK_PLUGIN(lo2do, 1);

// Statische/Globale Variablen sauber initialisieren
static uint8_t lo2do_is_set = 0;
static char lo2s_trace_path[256] = ""; 
static char job_cgroup_path[256] = "";

int _lo2do_cb(int val, const char *optarg, int remote) {
    // Falls remote (auf den Compute Nodes), aktivieren wir das Monitoring
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

int init_monitoring_process(const char *trace_path, const char *cgroup_path);
int stop_adopter_process();


int slurm_spank_init(spank_t sp, int ac, char **av) {
    // Nur die echten Optionen registrieren (Wächter am Ende weglassen!)
    spank_option_register(sp, &all_spank_options[0]);
    spank_option_register(sp, &all_spank_options[1]);
    return 0;
}


///////////////////!!!!!!!!!!!!!!!!!!!!!!!!!!!! Muss zu job epilog umgewandelt werden

int slurm_spank_init_post_opt(spank_t sp, int ac, char **av) {   
    // Wichtig: Nur ausführen, wenn der User die Option auch gewählt hat!
    if (!lo2do_is_set) return 0;
    if (spank_context() != S_CTX_REMOTE) return 0;

    unsigned int step_id;
    spank_get_item(sp, S_JOB_STEPID, &step_id);
    if (step_id != 0) {
        return 0; // Nur für den Haupt-Jobstep (meist Batch-Skript/Step 0)
    }

    unsigned int node_id;
    unsigned int job_id;
    spank_get_item(sp, S_JOB_NODEID, &node_id);
    spank_get_item(sp, S_JOB_ID, &job_id);

    // Falls kein Pfad angegeben wurde, aktuellen Pfad nehmen
    if (strlen(lo2s_trace_path) == 0) {
        if (!getcwd(lo2s_trace_path, sizeof(lo2s_trace_path))) {
            return -1;
        }
    }

    // Pfade sicher und sauber mit snprintf zusammenbauen
    char final_trace_path[512];
    snprintf(final_trace_path, sizeof(final_trace_path), "%s/lo2s_trace_%u_%u", 
             lo2s_trace_path, job_id, node_id);

    
    snprintf(job_cgroup_path, sizeof(job_cgroup_path), "/sys/fs/cgroup/system.slice/slurmstepd.scope/job_%u", job_id);

    if (access(job_cgroup_path, F_OK) != 0) {
        // Strategie B: Fallback auf den Pfad direkt unter system.slice
        
        snprintf(job_cgroup_path, sizeof(job_cgroup_path), 
                 "/sys/fs/cgroup/system.slice/job_%u", job_id);

        FILE *log_file = fopen("/tmp/spank_prolog.log", "a");
        if (log_file != NULL) {
            fprintf(log_file, "[LO2DO] Launching FALLBACK path for CP: %s\n", job_cgroup_path);
            fclose(log_file);
        }
        if (access(job_cgroup_path, F_OK) != 0) {
            FILE *log_file = fopen("/tmp/spank_prolog.log", "a");
            if (log_file != NULL) {
                fprintf(log_file, "[LO2DO] Launching FAILED von path for CP found: %s\n", job_cgroup_path);
                fclose(log_file);
            }
        }

    }


    // Logging für Debugging
    FILE *log_file = fopen("/tmp/spank_prolog.log", "a");
    if (log_file != NULL) {
        fprintf(log_file, "[LO2DO] Launching - TP: %s, CP: %s\n", final_trace_path, job_cgroup_path);
        fclose(log_file);
    }

    init_monitoring_process(final_trace_path, job_cgroup_path);
        
    return 0;
}


int slurm_spank_job_epilog(spank_t sp, int ac, char **av) {
    unsigned int job_id;
    spank_get_item(sp, S_JOB_NODEID, &job_id);
    if (job_id == 0) return 0;

    char cgroup_kill_path[256];
    
    // Strategie A: Versuche den Pfad mit "slurmstepd"
    snprintf(cgroup_kill_path, sizeof(cgroup_kill_path), 
             "/sys/fs/cgroup/system.slice/slurmstepd.scope/job_%u/cgroup.kill", job_id);

    
    if (access(cgroup_kill_path, F_OK) != 0) {
        // Strategie B: Fallback auf den Pfad direkt unter system.slice
        
        snprintf(cgroup_kill_path, sizeof(cgroup_kill_path), 
                 "/sys/fs/cgroup/system.slice/job_%u/cgroup.kill", job_id);
    }

    // Jetzt die cgroup atomar leeren
    FILE *kill_file = fopen(cgroup_kill_path, "w");
    if (kill_file != NULL) {
        fprintf(kill_file, "1");
        fclose(kill_file);
    } else {
        // Letzter rabiater Fallback per pkill, falls cgroup.kill fehlschlägt
        pid_t pid = fork();
        if (pid == 0) {
            freopen("/dev/null", "w", stdout);
            freopen("/dev/null", "w", stderr);
            char filter[256];
            snprintf(filter, sizeof(filter), "lo2s.*job_%u", job_id);
            char *args[] = {"pkill", "-f", filter, NULL};
            execvp(args[0], args);
            exit(1);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
        }
    }

    return 0;
}

/*
int sfghsfghadfghsfgh(spank_t sp, int ac, char **av) {

    return 0;
    unsigned int job_id;
    spank_get_item(sp, S_JOB_NODEID, &job_id);
    if (job_id == 0) return 0;

    FILE *log_file = fopen("/tmp/spank_prolog.log", "a");
    if (log_file != NULL) {
        fprintf(log_file, "[LO2DO] Epilog ended\n");
        fclose(log_file);
    }   

    slurm_spank_log("fuck");

    // Wir bauen den Pfad zur Kill-Datei der Job-Cgroup
    char cgroup_kill_path[256];
    snprintf(cgroup_kill_path, sizeof(cgroup_kill_path), 
             "/sys/fs/cgroup/system.slice/job_%u/cgroup.kill", job_id);
        slurm_spank_log("fuck2");

    log_file = fopen("/tmp/spank_prolog.log", "a");
    if (log_file != NULL) {
        fprintf(log_file, "[LO2DO] Epilog ended\n");
        fclose(log_file);
    }    
    // 1 in cgroup.kill schreiben killt ALLE verbliebenen Prozesse dieses Jobs auf dem Node
    FILE *kill_file = fopen(cgroup_kill_path, "w");
    if (kill_file != NULL) {
        fprintf(kill_file, "1");
        fclose(kill_file);
    } else {
        // Fallback: Falls cgroup v1 genutzt wird oder der Pfad anders ist,
        // nutzen wir pkill eingeschränkt auf die Job-Cgroup (über die PID, falls auffindbar)
        // Oder als ganz rabiater Fallback dein killall, verpackt in einen sicheren Fork:
        pid_t pid = fork();
        if (pid == 0) {
            freopen("/dev/null", "w", stdout);
            freopen("/dev/null", "w", stderr);
            // Nutze pkill -f mit dem spezifischen Trace-Pfad, um nur DEIN lo2s zu treffen!
            char filter[256];
            snprintf(filter, sizeof(filter), "lo2s.*job_%u", job_id);
            char *args[] = {"pkill", "-f", filter, NULL};
            execvp(args[0], args);
            exit(1);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
        }
    }
        slurm_spank_log("fuck3");

    log_file = fopen("/tmp/spank_prolog.log", "a");
    if (log_file != NULL) {
        fprintf(log_file, "[LO2DO] Epilog ended\n");
        fclose(log_file);
    }

    return 0;
}
*/
// Deine leicht korrigierte Stopp-Funktion (ohne unendlichen Fork, da execvp den Prozess ersetzt)
/*
int stop_adopter_process() {

    char *args[] = {"killall", "lo2s", NULL};
        
    // Wir forken hier, damit der Slurm-Epilog-Thread nicht stirbt!
    pid_t pid = fork();
    if (pid == 0) {
        freopen("/dev/null", "r", stdin);
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        execvp(args[0], args);
        exit(1); // Falls execvp fehlschlägt
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0); // Kurz warten, bis der Sender den Befehl abgesetzt hat
    }
    return 0;
}
*/
int init_monitoring_process(const char *trace_path, const char *cgroup_path) {
    pid_t pid1 = fork();

    if (pid1 < 0) {
        return -1; 
    }

    if (pid1 == 0) { // Erstes Kind
        pid_t pid2 = fork();

        if (pid2 < 0) {
            exit(1);
        }

        if (pid2 > 0) {
            exit(0); // Erstes Kind stirbt sofort -> Zweites Kind wird von init adoptiert
        }

        // --- Hier spricht das zweite Kind (Dämon) ---
        // Sicherstellen, dass stdout/stderr das Slurm-Spank-Plugin nicht blockieren
        freopen("/dev/null", "r", stdin);
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);

        char *args[] = {"lo2s", "-o", (char *)trace_path, "-aS", "--cgroup", (char *)cgroup_path, NULL};
         
        execvp(args[0], args);
        // Wenn execvp fehlschlägt:
        exit(1);
    } else { // Hauptprozess
        int status;
        waitpid(pid1, &status, 0); // Verhindert Zombie-Prozess des ersten Kindes
    }

    return 0;
}