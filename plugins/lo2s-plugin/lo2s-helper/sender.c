#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define FIFO_FILE "/tmp/adopter_pipe"

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Benutzung: %s <Job-ID> <Programm> [Argument1] [Argument2] ...\n", argv[0]);
        exit(1);
    }

    char pipe_path[64];
    snprintf(pipe_path, sizeof(pipe_path), "%s%s", FIFO_FILE, argv[1]);


    // Pipe zum Schreiben öffnen
    int fifo_fd = open(pipe_path, O_WRONLY);
    if (fifo_fd < 0) {
        perror("Dauerdienst läuft offenbar nicht (Pipe konnte nicht geöffnet werden)");
        exit(1);
    }

    // Alle Argumente nacheinander in die Pipe schreiben, getrennt durch Leerzeichen
    for (int i = 2; i < argc; i++) {
        dprintf(fifo_fd, "%s", argv[i]);
        if (i < argc - 1) {
            dprintf(fifo_fd, " "); // Leerzeichen zwischen den Argumenten
        }
    }
    // Am Ende das Newline für das 'fgets' des Dauerdienstes senden
    dprintf(fifo_fd, "\n");

    close(fifo_fd);
    printf("[Starter] Befehl erfolgreich an Dauerdienst gesendet.\n");

    return 0;
}