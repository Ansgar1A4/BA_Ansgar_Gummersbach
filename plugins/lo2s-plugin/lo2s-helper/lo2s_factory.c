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

#define FIFO_FILE "/tmp/factory_pipe"
#define BUFFER_SIZE 256
#define DELIMITER ";"

int job_id = -1;
pid_t current_lo2s_pid = -1; // HIER: Merkt sich die PID des laufenden lo2s-Prozesses

int main(int argc, char *argv[]) {

    if (argc == 1) return -1;

    char pipe_path[128];
    job_id = atoi(argv[1]);
    snprintf(pipe_path, sizeof(pipe_path), "%s%d", FIFO_FILE, job_id);

    if (argc == 2) { // init controll-process (lo2s_factory [JOBID])
        
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
            // 1. Öffnen mit O_NONBLOCK verhindert das Blockieren, wenn kein Writer da ist
            fifo_fd = open(pipe_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);            
            if (fifo_fd < 0) {
                continue;
            }

            // 2. O_NONBLOCK wieder ausschalten, damit fgets() danach sauber blockiert
            int flags = fcntl(fifo_fd, F_GETFL);
            fcntl(fifo_fd, F_SETFL, flags & ~O_NONBLOCK);

            FILE *fifo_stream = fdopen(fifo_fd, "r");
            if (fifo_stream == NULL) {
                close(fifo_fd);
                continue;
            }

            // Zeilenweise aus der Pipe lesen
            while (fgets(buffer, BUFFER_SIZE, fifo_stream) != NULL) {

                buffer[strcspn(buffer, "\n")] = 0; // Newline entfernen
                if (strlen(buffer) == 0) continue;

                log_file = fopen("/tmp/spank_prolog.log", "a");
                if (log_file != NULL) {
                    fprintf(log_file, "GETS: %s\n", buffer);
                    fclose(log_file);
                }
                
                // EXIT BEFEHL VERARBEITEN
                if (strcmp(buffer, "exit_lo2s") == 0) {
                    // PID aus Datei lesen
                    FILE *f = fopen("/tmp/lo2s_current_pid", "r");
                    if (f) {
                        fscanf(f, "%d", &current_lo2s_pid);
                        fclose(f);
                    }
                    
                    if (current_lo2s_pid > 0) {
                        kill(-current_lo2s_pid, SIGINT);
                        sleep(3);
                        // Wir warten, bis der Prozess wirklich weg ist.

                        for(int i = 0; i < 20; i++) {
                            // kill gibt -1 zurück (und setzt errno auf ESRCH), wenn der Prozess nicht mehr existiert
                            if (kill(current_lo2s_pid, 0) == -1) {
                                break; // Prozess ist erfolgreich beendet
                            }
                            usleep(500000); // 500ms warten
                        }
                        
                        // 3. Dateisystem-Flush erzwingen, bevor wir den Prozess "vergessen"
                        sync(); 
                    }
                    fclose(fifo_stream); 
                    unlink(pipe_path);
                    exit(0);
                }

                if(getenv("LOS_TRACE") != NULL) continue;
                setenv("LOS_TRACE", "true", 1);


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
                    
                    if (trace_path == NULL) {
                        exit(1);
                    }

                    // KEIN clearenv()! Wir erweitern nur das bestehende Environment
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
                    char *args[] = {"/usr/local/bin/lo2s", "-o", trace_path, "-aS", "--cgroup", found_path, NULL};

                    if (execvp(args[0], args) < 0) {
                        perror("execvp fehlgeschlagen");
                        exit(1);
                    }
                } else { 
                    // Elternprozess-Teil
                    current_lo2s_pid = pid;
                    FILE *f = fopen("/tmp/lo2s_current_pid", "w");
                    if (f) { fprintf(f, "%d", pid); fclose(f); }
                }
                
            }
            fclose(fifo_stream); 
        }
    } else if (argc == 3) { // stop control-process
        int fifo_fd = open(pipe_path, O_WRONLY);
        if (fifo_fd < 0) {
            exit(1);
        }
        dprintf(fifo_fd, "exit_lo2s\n");
        close(fifo_fd);
    } else if (argc == 4) { // start lo2s
        int fifo_fd = open(pipe_path, O_WRONLY);
        if (fifo_fd < 0) {
            exit(1);
        }
        dprintf(fifo_fd, "%s%s%s\n", argv[2], DELIMITER, argv[3]);
        close(fifo_fd);
    } else {
        exit(1);
    }
    return 0;
}