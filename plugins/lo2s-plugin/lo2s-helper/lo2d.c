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

#define FIFO_FILE "/tmp/lo2d_pipe"
// TODO: Make this configurable 
#define LO2S_PATH "/usr/local/bin/lo2s"
#define BUFFER_SIZE 256
#define DELIMITER ";"

int job_id = -1;
pid_t current_lo2s_pid = -1;
volatile sig_atomic_t lo2s_exited = 0;

static void log_lo2d(const char *format, ...) {
    FILE *log_file = fopen("/tmp/spank_lo2do.log", "a");
    if (!log_file) return;
    va_list ap;
    va_start(ap, format);
    vfprintf(log_file, format, ap);
    va_end(ap);
    fflush(log_file);
    fclose(log_file);
}

static void handle_sigchld(int sig) {
    (void)sig;
    int saved_errno = errno;
    while (1) {
        pid_t pid = waitpid(-1, NULL, WNOHANG);
        if (pid <= 0) break;
        if (pid == current_lo2s_pid) {
            lo2s_exited = 1;
        }
    }
    errno = saved_errno;
}

int main(int argc, char *argv[]) {

    if (argc != 2) return -1;

    char pipe_path[128];
    job_id = atoi(argv[1]);
    snprintf(pipe_path, sizeof(pipe_path), "%s%d", FIFO_FILE, job_id);
    
    char buffer[BUFFER_SIZE];
    int fifo_fd;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);

    sa.sa_flags = SA_NOCLDSTOP;
    sa.sa_handler = handle_sigchld;

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

    while (1) {
        // 1. Blockierend öffnen: Wartet effizient bis ein Writer vorhanden ist.
        fifo_fd = open(pipe_path, O_RDONLY | O_CLOEXEC);
        if (fifo_fd < 0) {
            if (errno == EINTR) {
                if (lo2s_exited) {
                    log_lo2d("[LO2D] lo2s exited, shutting down daemon\n");
                    unlink(pipe_path);
                    return 0;
                }
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
            log_lo2d("[LO2D] received command: %s, pid: %d\n", buffer, current_lo2s_pid);
            // EXIT BEFEHL VERARBEITEN
            if (strcmp(buffer, "exit_lo2s") == 0) {
                // PID aus Datei lesen
                // Close procedure if lo2s process has not been started
                if (current_lo2s_pid < 0) {
                    fclose(fifo_stream); 
                    unlink(pipe_path);
                    exit(0);
                }
                
                if (current_lo2s_pid > 0) {
                    log_lo2d("[LO2D] stop: found lo2s pgid=%d\n", current_lo2s_pid);
                    if (kill(-current_lo2s_pid, 0) == 0) {
                        int wait_checks = 0;
                        int wait_max = 20;
                        while (wait_checks < wait_max && kill(current_lo2s_pid, 0) == 0) {
                            usleep(100000);
                            wait_checks++;
                        }
                        if (kill(current_lo2s_pid, 0) != 0) {
                            log_lo2d("[LO2D] stop: lo2s already exited after %d checks\n", wait_checks);
                        } else {
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
                        log_lo2d("[LO2D] found cgroup: %s\n", found_path);
                        found_path[strcspn(found_path, "\n")] = 0;
                    }
                    pclose(fp);
                }

                if (strlen(found_path) == 0) {
                    fprintf(stderr, "[FATAL] Cgroup für Job %d nirgends gefunden!\n", job_id);
                    exit(1);
                }
                
                //snprintf(found_path, sizeof(found_path), "/sys/fs/cgroup/system.slice/slurmstepd.scope/job_%d");
                // Direkt ausführen ohne den Bash-Umweg, da wir die Umgebung jetzt via task_exit sichern
                char *args[32];
                int arg_i = 0;
                args[arg_i++] = LO2S_PATH;
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
            }
            
        }
        fclose(fifo_stream); 
    }
    
    return 0;
}