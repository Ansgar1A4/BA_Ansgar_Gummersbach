#include <stddef.h>
#include <string.h>
#include <slurm/spank.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>


SPANK_PLUGIN(lo2do, 1);

int start_adopter_process();
int stop_adopter_process();

//static int _opt_lo2do_enable_cb(int val, const char *optarg, int remote){}

struct spank_option spank_options[] = {
    {"lo2do", NULL, "Enable lo2s tracing for each jobsteps of the given batch job", 0, 0, NULL},
    SPANK_OPTIONS_TABLE_END
};

int slurm_spank_init(spank_t sp, int ac, char **av)
{
    // Register the plugin option
    if (spank_context() == S_CTX_ALLOCATOR) {
        spank_option_register(sp, &spank_options[0]);
    }
    if (spank_context() == S_CTX_SLURMD) {
        // Starting the adopter process in slurmd context, so it will be available for all jobs running on this node
        start_adopter_process();
    }
    return 0;
}

int slurm_spank_slurmd_exit(spank_t sp, int ac, char **av)
{
    if (spank_context() == S_CTX_SLURMD) {
        // Stopping the adopter process when slurmd is exiting
        stop_adopter_process();
    }
    return 0;
}



int slurm_spank_job_prolog(spank_t sp, int ac, char **av) {
    fprintf(stderr, "lo2do job prolog called in context: %d\n", spank_context());
    
    unsigned int job_id;
    spank_err_t rc = spank_get_item(sp, S_JOB_ID, &job_id);


    FILE *log_file = fopen("/tmp/spank_prolog.log", "w");
    if (log_file != NULL) {
        fprintf(log_file, "lo2do job prolog called in context: %d for job: %d, err: %s\n", spank_context(), job_id, spank_strerror(rc));
        fclose(log_file);
    } 

    return 0;
}

int slurm_spank_job_epilog(spank_t sp, int ac, char **av) {
    fprintf(stderr, "lo2do job epilog called in context: %d\n", spank_context());
    
    unsigned int job_id;
    spank_err_t rc = spank_get_item(sp, S_JOB_ID, &job_id);

    FILE *log_file = fopen("/tmp/spank_prolog.log", "a");
    if (log_file != NULL) {
        fprintf(log_file, "lo2do job epilog called in context: %d for job: %d, err: %s\n", spank_context(), job_id, spank_strerror(rc));
        fclose(log_file);
    } 
    //stop_adopter_process();

    return 0;
}


int slurm_spank_task_init(spank_t sp, int ac, char **av)
{   


    unsigned int node_id;
    spank_err_t rc = spank_get_item(sp, S_JOB_NODEID, &node_id);

    unsigned int job_id;
    spank_err_t rc_job = spank_get_item(sp, S_JOB_ID, &job_id);

    int argc;
    char **argv;
    spank_err_t rc2 = spank_get_item(sp, S_JOB_ARGV, &argc, &argv);

    char env_name[256];
    snprintf(env_name, sizeof(env_name), "LO2S_DEAMON_SET_%u_%u", node_id, job_id);
    char env_val[256] = "";
    spank_getenv(sp, env_name, env_val, sizeof(env_val));


    if (strlen(env_val) == 0) {
        spank_setenv(sp, env_name, "true", 1);
        fprintf(stderr, "lo2do task init: %s not set, context: %d\n", env_name, spank_context());
    } else {
        fprintf(stderr, "lo2do task init: %s is set, context: %d\n", env_name, spank_context());
    }

    /*
    FILE *log_file = fopen("/tmp/spank_prolog.log", "a");
    if (log_file != NULL) {
        fprintf(log_file, "lo2do task init called in context: %d, ID: %d, err: %s, err2: %s, count: %d\n", spank_context(), node_id, spank_strerror(rc), spank_strerror(rc2), argc);
        for (int i = 0; i < argc; i++) {
            fprintf(log_file, "argv[%d]: %s\n", i, argv[i]);
        }
        fclose(log_file);
    } 
    */
    return 0;
}


/*
int slurm_spank_local_user_init(spank_t sp, int ac, char **av)
{
    
    if (spank_option_getopt(sp, &spank_options[0], NULL) == ESPANK_SUCCESS) {
        //spank_setenv(sp, "LO2S_PLUGIN_ENABLED", "true", 256);
        fprintf(stderr, "lo2do enabled: %d\n", spank_context());
    }else {
        fprintf(stderr, "lo2do option not set, context: %d\n", spank_context());
    }

    return 0;
}
*/


int start_adopter_process() {
    pid_t pid = fork();

    if (pid < 0) {
        perror("Fork for lo2s_adopter failed");
    } else if (pid == 0) {

        char *args[] = {"lo2s_adopter", NULL};
        
        if (execvp(args[0], args) < 0) {
            perror("Exec for lo2s_adopter failed");
            exit(1);
        }
    }
}

int stop_adopter_process() {
    char *args[] = {"lo2s_sender", "exit_lo2s", NULL};
        
    if (execvp(args[0], args) < 0) {
        perror("Exec for lo2s_sender failed");
        exit(1);
    }
    
}


