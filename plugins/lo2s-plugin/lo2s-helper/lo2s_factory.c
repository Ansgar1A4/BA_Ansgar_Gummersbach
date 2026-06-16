// lo2s-factory file


#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <threads.h>

#define FIFO_FILE "/tmp/factory_pipe"
#define BUFFER_SIZE 256
#define DELIMITER ";"

int job_id = -1;

int main(int argc, char *argv[]) {

    if (argc == 1) return -1;

    char pipe_path[128];
    job_id = atoi(argv[1]);
    snprintf(pipe_path, sizeof(pipe_path), "%s%d", FIFO_FILE, job_id);

    if (argc == 2) { // init controll-process (lo2s_factory [JOBID])
        if(strcmp(getenv("LO2S_is_set"), "true") == 0) return 0; 
        setenv("LO2S_is_set", "true", 1);
        char buffer[BUFFER_SIZE];
        int fifo_fd;

        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sigemptyset(&sa.sa_mask); 
        
        // SA_NOCLDWAIT sorgt dafür, dass Kinder NIEMALS zu Zombies werden!
        sa.sa_flags = SA_RESTART | SA_NOCLDSTOP | SA_NOCLDWAIT; 
        sa.sa_handler = SIG_DFL; // Standard-Handler reicht dank SA_NOCLDWAIT
        
        if (sigaction(SIGCHLD, &sa, NULL) < 0) {
            perror("Signal-Setup fehlgeschlagen");
            exit(1);
        }

        // Named Pipe erstellen
        mkfifo(pipe_path, 0600);

        while (1) {

            // 1. Öffnen mit O_NONBLOCK verhindert das Blockieren, wenn kein Writer da ist
            fifo_fd = open(pipe_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);            
            if (fifo_fd < 0) {
                perror("Fehler beim Öffnen der Pipe");
                exit(1);
            }

            // 2. O_NONBLOCK wieder ausschalten, damit fgets() danach sauber blockiert
            int flags = fcntl(fifo_fd, F_GETFL);
            fcntl(fifo_fd, F_SETFL, flags & ~O_NONBLOCK);

            // fdopen durchführen
            FILE *fifo_stream = fdopen(fifo_fd, "r");
            if (fifo_stream == NULL) {
                perror("fdopen fehlgeschlagen");
                close(fifo_fd);
                continue;
            }


            // Zeilenweise aus der Pipe lesen
            while (fgets(buffer, BUFFER_SIZE, fifo_stream) != NULL) {

                 FILE* log_file = fopen("/tmp/spank_prolog.log", "a");
                if (log_file != NULL) {
                fprintf(log_file, "GETS: %s", buffer);
                fclose(log_file);
                }
                buffer[strcspn(buffer, "\n")] = 0; // Newline entfernen

                if (strlen(buffer) == 0) continue;
                
                if (strcmp(buffer, "exit_lo2s") == 0) {
                    log_file = fopen("/tmp/spank_prolog.log", "a");
                    if (log_file != NULL) {
                        fprintf(log_file, "init exit\n");
                        fclose(log_file);
                    }
                    printf("[Dauerdienst] Exit-Befehl erhalten. Fahre herunter...\n");
                    fclose(fifo_stream); // Schließt auch fifo_fd
                    unlink(pipe_path);

                    //TODO: schließe lo2s

                    exit(0);
                }

                pid_t pid = fork();

                if (pid < 0) {
                    perror("Fork fehlgeschlagen");
                } else if (pid == 0) {
                            // Im Kindprozess: Stream schließen
                    fclose(fifo_stream);

                    freopen("/tmp/lo2s_error.log", "a", stderr);
                    freopen("/tmp/lo2s_output.log", "a", stdout);

                    char *trace_path = strtok(buffer, DELIMITER);
                    char *cgroup_path = strtok(NULL, DELIMITER);
                    
                    if (trace_path == NULL || cgroup_path == NULL) {
                        fprintf(stderr, "[Kind] Fehler: Unvollständige Argumente empfangen!\n");
                        exit(1);
                    }


                    


                    char *args[] = {"/usr/local/bin/lo2s", "-o", trace_path, "-aS", "--cgroup", cgroup_path, NULL};

                    // Jetzt übergibst du das dynamisch gefüllte Array an execvp
                    if (execvp(args[0], args) < 0) {
                        
                        log_file = fopen("/tmp/spank_prolog.log", "a");
                        if (log_file != NULL) {
                            fprintf(log_file, "EXEC FAILED for: %s; %s\n", trace_path, cgroup_path);
                            fclose(log_file);
                        }

                        exit(1);
                    }
                    log_file = fopen("/tmp/spank_prolog.log", "a");
                    if (log_file != NULL) {
                        fprintf(log_file, "[Info] EXEC for: %s; %s\n", trace_path, cgroup_path);
                        fclose(log_file);
                    }
                }
                // Der Elternprozess läuft dank SA_NOCLDWAIT sofort weiter, 
                // ohne sich je um wait() kümmern zu müssen.
            }

            
            // Wichtig: Schließt den Stream UND den darunterliegenden fifo_fd sauber
            fclose(fifo_stream); 
          
        }
        

        
    }else if (argc == 3) // stop control-process and terminate lo2s (lo2s_factory [JOB_ID] exit_lo2s)
    {
        int fifo_fd = open(pipe_path, O_WRONLY);
        if (fifo_fd < 0) {
            perror("Dauerdienst läuft offenbar nicht (Pipe konnte nicht geöffnet werden)");
            exit(1);
        }
        dprintf(fifo_fd, "exit_lo2s\n");
        // Am Ende das Newline für das 'fgets' des Dauerdienstes senden
        FILE *log_file = fopen("/tmp/spank_prolog.log", "a");
        if (log_file != NULL) {
            fprintf(log_file, "[F] init exit\n");
            fclose(log_file);
        }
        close(fifo_fd);
    }else if (argc == 4) // start lo2s (lo2s_factory [JOBID] [trace_path] [cgroup_path])
    {
        int fifo_fd = open(pipe_path, O_WRONLY);
        if (fifo_fd < 0) {
            perror("Dauerdienst läuft offenbar nicht (Pipe konnte nicht geöffnet werden)");
            exit(1);
        }
        
        dprintf(fifo_fd, "%s%s%s\n", argv[2], DELIMITER, argv[3]);
        close(fifo_fd);
        
    }else{
        fprintf(stderr, "No job ID or too many arguments provided to adopter process, exiting.\n");
        exit(1);
    }

    return 0;
}