#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>

#define FIFO_FILE "/tmp/adopter_pipe"
#define BUFFER_SIZE 256

int main() {
    char buffer[BUFFER_SIZE];
    int fifo_fd;

    // Signal-Handling: Zombies direkt vom Kernel verhindern lassen
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
    mkfifo(FIFO_FILE, 0666);

    while (1) {
        // Blockiert, bis ein Schreibprozess die Pipe öffnet
        fifo_fd = open(FIFO_FILE, O_RDONLY);
        if (fifo_fd < 0) {
            perror("Fehler beim Öffnen der Pipe");
            exit(1);
        }

        // fdopen einmalig pro geöffnetem Deskriptor durchführen
        FILE *fifo_stream = fdopen(fifo_fd, "r");
        if (fifo_stream == NULL) {
            perror("fdopen fehlgeschlagen");
            close(fifo_fd);
            continue;
        }

        // Zeilenweise aus der Pipe lesen
        while (fgets(buffer, BUFFER_SIZE, fifo_stream) != NULL) {
            buffer[strcspn(buffer, "\n")] = 0; // Newline entfernen

            if (strlen(buffer) == 0) continue;
            
            if (strcmp(buffer, "exit_lo2s") == 0) {
                printf("[Dauerdienst] Exit-Befehl erhalten. Fahre herunter...\n");
                fclose(fifo_stream); // Schließt auch fifo_fd
                unlink(FIFO_FILE);
                exit(0);
            }

            pid_t pid = fork();

            if (pid < 0) {
                perror("Fork fehlgeschlagen");
            } else if (pid == 0) {
                           // Im Kindprozess: Stream schließen
                fclose(fifo_stream);
                        
                // Array für die Argumente vorbereiten (z.B. maximal 64 Argumente)
                char *args[64]; 
                int i = 0;
                        
                // Den Buffer an den Leerzeichen zerlegen
                char *token = strtok(buffer, " ");
                while (token != NULL && i < 63) {
                    args[i++] = token;
                    token = strtok(NULL, " ");
                }
                args[i] = NULL; // Das Array MUSS mit NULL enden!
            
                if (args[0] == NULL) {
                    exit(0); // Leerer Befehl, nichts zu tun
                }
            
                // Jetzt übergibst du das dynamisch gefüllte Array an execvp
                if (execvp(args[0], args) < 0) {
                    perror("Exec mit Argumenten fehlgeschlagen");
                    exit(1);
                }
            }
            // Der Elternprozess läuft dank SA_NOCLDWAIT sofort weiter, 
            // ohne sich je um wait() kümmern zu müssen.
        }

        // Wichtig: Schließt den Stream UND den darunterliegenden fifo_fd sauber
        fclose(fifo_stream); 
    }

    return 0;
}