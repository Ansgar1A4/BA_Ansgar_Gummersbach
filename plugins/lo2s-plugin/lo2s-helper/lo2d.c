#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

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

// TODO: Make this configurable 
#define FIFO_FILE "/tmp/lo2d_pipe"
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

static void reap_any_children(pid_t target_pid, int *saw_target) {
    int saved_errno = errno;
    while (1) {
        int status = 0;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0) break;
        if (target_pid > 0 && pid == target_pid) {
            *saw_target = 1;
        }
        if (pid == current_lo2s_pid) {
            lo2s_exited = 1;
            current_lo2s_pid = -1;
        }
    }
    errno = saved_errno;
}

static int wait_for_child_exit(pid_t pid, int timeout_ms) {
    int elapsed_ms = 0;
    while (elapsed_ms < timeout_ms) {
        int status = 0;
        
        pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            log_lo2d("[KILL]: 1, %d\n", status);
            return 1;
        }
        if (waited < 0 && errno != EINTR && errno != ECHILD) {
            break;
        }
        if (kill(pid, 0) != 0) {
            log_lo2d("[KILL]: 2\n");
            return 1;
        }
        usleep(100000);
        
        elapsed_ms += 100;
    }
    return 0;
}

static int stop_child_process(pid_t pid) {
    if (pid <= 0) {
        return 1;
    }

    if (kill(pid, 0) != 0) {
        return 1;
    }

    log_lo2d("[LO2D] stop: sending SIGINT to lo2s pid=%d\n", pid);
    if (kill(-pid, SIGINT) != 0) {
        log_lo2d("[LO2D] stop: failed to send SIGINT to lo2s pid=%d: %s\n", pid, strerror(errno));
        return 0;
    }


    if (wait_for_child_exit(pid, 300000)) {
        log_lo2d("[LO2D] stop: lo2s pid=%d exited cleanly after SIGINT\n", pid);
        return 1;
    }

    log_lo2d("[LO2D] stop: lo2s pid=%d did not exit after SIGINT, leaving it running\n", pid);
    return 0;
}

static void handle_sigchld(int sig) {
    (void)sig;
    int saw_target = 0;
    reap_any_children(current_lo2s_pid, &saw_target);
    if (saw_target) {
        lo2s_exited = 1;
    }
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
        int dummy_saw_target = 0;
        //reap_any_children(-1, &dummy_saw_target);

        // 1. Blockierend öffnen
        fifo_fd = open(pipe_path, O_RDONLY | O_CLOEXEC);
        log_lo2d("Found something");
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
                if (current_lo2s_pid > 0) {
                    log_lo2d("[LO2D] stop: found lo2s pgid=%d\n", current_lo2s_pid);
                    if (kill(current_lo2s_pid, 0) == 0) {
                        if (kill(current_lo2s_pid, 0) != 0) {
                            log_lo2d("[LO2D] stop: lo2s already exited\n");
                        } else {
                            stop_child_process(current_lo2s_pid);
                        }
                    } else {
                        log_lo2d("[LO2D] stop: lo2s pgid=%d not alive\n", current_lo2s_pid);
                    }
                    //reap_any_children(-1, &dummy_saw_target);
                    current_lo2s_pid = -1;
                    lo2s_exited = 0;
                    sync(); 
                }
                fclose(fifo_stream); 
                unlink(pipe_path);
                _exit(0);
                
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
                    _exit(1);
                }

                // KEIN clearenv()! Wir erweitern nur das bestehende Environment
                // TODO: Path nur auf lo2s-Pfad setzen
                setenv("PATH", "/usr/bin:/usr/local/bin:/usr/sbin:/sbin", 1);

                // Fehler-Logs wieder aktivieren!
                freopen("/tmp/spank_lo2do.log", "a", stderr);
                freopen("/tmp/spank_lo2do.log", "a", stdout);

                // Suche nach der Cgroup
                char found_path[512] = {0};
                char cmd[256];
                snprintf(cmd, sizeof(cmd), "/usr/bin/find /sys/fs/cgroup -name 'job_%d' | head -n 1", job_id);

                //TODO: clearenv + set lo2s as path-envar
                
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
                    _exit(1);
                }
                
                //snprintf(found_path, sizeof(found_path), "/sys/fs/cgroup/system.slice/slurmstepd.scope/job_%d");
                char *args[32];
                int arg_i = 0;
                args[arg_i++] = LO2S_PATH;
                if (extra_args != NULL && strlen(extra_args) > 0) {
                    char *opt = strtok(extra_args, " ");
                    while (opt != NULL && arg_i < (int)(sizeof(args)/sizeof(args[0]) - 1)) {
                        args[arg_i++] = opt;
                        opt = strtok(NULL, " ");
                    }
                }
                args[arg_i++] = "-o";
                args[arg_i++] = trace_path;
                args[arg_i++] = "-AS";
                args[arg_i++] = "--cgroup";
                args[arg_i++] = found_path;
                args[arg_i++] = "--dwarf";
                args[arg_i++] = "full";
                args[arg_i++] = "-g";

                args[arg_i] = NULL;


                setenv("DEBUGINFOD_URLS", "https://rockylinux.org", 1);
                if (execvp(args[0], args) < 0) {
                    perror("execvp fehlgeschlagen");
                    _exit(1);
                }
            } else { 
                // Elternprozess-Teil
                current_lo2s_pid = pid;
                lo2s_exited = 0;
            }
            
        }
        fclose(fifo_stream); 
    }
    
    return 0;
}