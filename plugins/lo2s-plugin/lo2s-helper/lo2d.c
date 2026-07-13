#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <stdarg.h>

#define FIFO_FILE "/tmp/factory_pipe"
#define BUFFER_SIZE 256
#define DELIMITER ";"

int job_id = -1;
pid_t current_lo2s_pid = -1;

static void log_lo2d(const char *format, ...) {
    FILE *log_file = fopen("/tmp/spank_prolog.log", "a");
    if (!log_file) return;
    va_list ap;
    va_start(ap, format);
    vfprintf(log_file, format, ap);
    va_end(ap);
    fflush(log_file);
    fclose(log_file);
}

int main(int argc, char *argv[]) {

    if (argc != 2) return -1;

    char pipe_path[128];
    job_id = atoi(argv[1]);
    snprintf(pipe_path, sizeof(pipe_path), "%s%d", FIFO_FILE, job_id);

        
    // FIX: Sicherer getenv-Vergleich verhindert Segfault!
    char *env_val = getenv("LO2S_is_set");
    if (env_val != NULL && strcmp(env_val, "true") == 0) {
        return 0; 
    }
    setenv("LO2S_is_set", "true", 1);
    
    char buffer[BUFFER_SIZE];
    int fifo_fd;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask); 
    
    // SA_NOCLDWAIT sorgt dafür, dass Kinder NIEMALS zu Zombies werden!
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP | SA_NOCLDWAIT; 
    sa.sa_handler = SIG_DFL; 
    
    if (sigaction(SIGCHLD, &sa, NULL) < 0) {
        perror("Signal-Setup fehlgeschlagen");
        exit(1);
    }

    // Named Pipe erstellen
    unlink(pipe_path); // Alte Reste entfernen falls vorhanden
    if (mkfifo(pipe_path, 0600) < 0) {
        perror("mkfifo fehlgeschlagen");
        exit(1);
    }

    // Signal-Log für den Start des Daemons
    FILE* log_file = fopen("/tmp/spank_prolog.log", "a");
    if (log_file != NULL) {
        fprintf(log_file, "in factory2 (Dauerdienst bereit)\n");
        fclose(log_file);
    }

    while (1) {
        // 1. Blockierend öffnen: Wartet effizient bis ein Writer vorhanden ist.
        fifo_fd = open(pipe_path, O_RDONLY | O_CLOEXEC);
        if (fifo_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("open fehlgeschlagen");
            sleep(1);
            continue;
        }

        FILE *fifo_stream = fdopen(fifo_fd, "r");
        if (fifo_stream == NULL) {
            close(fifo_fd);
            continue;
        }

        // Zeilenweise aus der Pipe lesen
        while (fgets(buffer, BUFFER_SIZE, fifo_stream) != NULL) {

            buffer[strcspn(buffer, "\n")] = 0; // Newline entfernen
            if (strlen(buffer) == 0) continue;
            
            // EXIT BEFEHL VERARBEITEN
            if (strcmp(buffer, "exit_lo2s") == 0) {
                // PID aus Datei lesen
                char pid_file[256] = "";
                snprintf(pid_file, sizeof(pid_file), "/tmp/lo2s_pid_%d", job_id);
                FILE *f = fopen(pid_file, "r");
                if (f) {
                    fscanf(f, "%d", &current_lo2s_pid);
                    fclose(f);
                }
                
                if (current_lo2s_pid > 0) {
                    log_lo2d("[LO2D] stop: found lo2s pgid=%d\n", current_lo2s_pid);
                    if (kill(-current_lo2s_pid, 0) == 0) {
                        // Give lo2s a short time to flush output before terminating it.
                        sleep(2);
                        if (kill(-current_lo2s_pid, SIGINT) == 0) {
                            log_lo2d("[LO2D] stop: sent SIGINT to pgid=%d\n", current_lo2s_pid);
                        } else {
                            log_lo2d("[LO2D] stop: failed to send SIGINT to pgid=%d: %s\n", current_lo2s_pid, strerror(errno));
                        }
                        for (int i = 0; i < 20; i++) {
                            if (kill(current_lo2s_pid, 0) == -1) {
                                log_lo2d("[LO2D] stop: lo2s exited after %d checks\n", i + 1);
                                break;
                            }
                            usleep(500000);
                        }
                        if (kill(current_lo2s_pid, 0) == 0) {
                            log_lo2d("[LO2D] stop: lo2s still alive after timeout, sending SIGKILL pgid=%d\n", current_lo2s_pid);
                            kill(-current_lo2s_pid, SIGKILL);
                        }
                    } else {
                        log_lo2d("[LO2D] stop: lo2s pgid=%d not alive\n", current_lo2s_pid);
                    }
                    sync(); 
                }
                fclose(fifo_stream); 
                unlink(pipe_path);
                exit(0);
            }

            if(getenv("LO2S_TRACE") != NULL) continue;
            setenv("LO2S_TRACE", "true", 1);


            // TRACE STARTEN (FORK)
            pid_t pid = fork();

            if (pid < 0) {
                perror("Fork fehlgeschlagen");
            } else if (pid == 0) {

                setpgid(0, 0);
                fclose(fifo_stream);

                char buffer_copy[BUFFER_SIZE];
                strncpy(buffer_copy, buffer, BUFFER_SIZE);

                char *trace_path = strtok(buffer_copy, DELIMITER);
                char *cgroup_path = strtok(NULL, DELIMITER);
                char *extra_args = strtok(NULL, DELIMITER);
                
                if (trace_path == NULL) {
                    exit(1);
                }

                // KEIN clearenv()! Wir erweitern nur das bestehende Environment
                // TODO: Path nur auf lo2s-Pfad setzen
                setenv("PATH", "/usr/bin:/usr/local/bin:/usr/sbin:/sbin", 1);

                // Fehler-Logs wieder aktivieren!
                freopen("/tmp/lo2s_error.log", "a", stderr);
                freopen("/tmp/lo2s_output.log", "a", stdout);

                // Suche nach der Cgroup
                char found_path[512] = {0};
                char cmd[256];
                snprintf(cmd, sizeof(cmd), "/usr/bin/find /sys/fs/cgroup -name 'job_%d' | head -n 5", job_id);

                FILE *fp = popen(cmd, "r");
                if (fp) {
                    if (fgets(found_path, sizeof(found_path), fp) != NULL) {
                        found_path[strcspn(found_path, "\n")] = 0;
                    }
                    pclose(fp);
                }

                log_file = fopen("/tmp/spank_prolog.log", "a");
                if (log_file != NULL) {
                    fprintf(log_file, " found cgroup: %s\n", found_path);
                    fclose(log_file);
                }


                if (strlen(found_path) == 0) {
                    fprintf(stderr, "[FATAL] Cgroup für Job %d nirgends gefunden!\n", job_id);
                    exit(1);
                }
                
                //snprintf(found_path, sizeof(found_path), "/sys/fs/cgroup/system.slice/slurmstepd.scope/job_%d");
                // Direkt ausführen ohne den Bash-Umweg, da wir die Umgebung jetzt via task_exit sichern
                char *args[32];
                int arg_i = 0;
                args[arg_i++] = "/usr/local/bin/lo2s";
                args[arg_i++] = "-o";
                args[arg_i++] = trace_path;
                args[arg_i++] = "-aS";
                args[arg_i++] = "--cgroup";
                args[arg_i++] = found_path;

                if (extra_args != NULL && strlen(extra_args) > 0) {
                    char *opt = strtok(extra_args, " ");
                    while (opt != NULL && arg_i < (int)(sizeof(args)/sizeof(args[0]) - 1)) {
                        args[arg_i++] = opt;
                        opt = strtok(NULL, " ");
                    }
                }
                args[arg_i] = NULL;

                if (execvp(args[0], args) < 0) {
                    perror("execvp fehlgeschlagen");
                    exit(1);
                }
            } else { 
                // Elternprozess-Teil
                current_lo2s_pid = pid;
                char pid_file[256] = "";
                snprintf(pid_file, sizeof(pid_file), "/tmp/lo2s_pid_%d", job_id);
                FILE *f = fopen(pid_file, "w");
                if (f) { fprintf(f, "%d", pid); fclose(f); }
            }
            
        }
        fclose(fifo_stream); 
    }
    
    return 0;
}