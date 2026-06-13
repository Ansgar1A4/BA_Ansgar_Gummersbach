#include <stddef.h>
#include <string.h>
#include <slurm/spank.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdint.h>
#include <threads.h>


SPANK_PLUGIN(lo2do, 1);

u_int8_t lo2do_is_set = 0;
char lo2s_trace_path[128] = "";



int _lo2do_cb(int val,const char *optarg, int remote){
    if(!remote) return 0;
    lo2do_is_set = 1;
    sprintf(lo2s_trace_path, optarg);
        
    return 0;
}


struct spank_option all_spank_options[] = {
    {"lo2do", "TRACE_PATH", 
    "Enable lo2s System-Monitoring for given job per node, writing generated otf2-traces to given TRACE_PATH (default: pwd)",
    2, 0, _lo2do_cb},
    // TODO
    {"lo2do_cgroup_path", "MODE_OPT", "Mode is eather \"attach\" or \"system_monitor\"", 1, 0, NULL},
    SPANK_OPTIONS_TABLE_END
};

int init_monitoring_process(char *trace_path);
int stop_monitoring_process(int job_id);


int slurm_spank_init(spank_t sp, int ac, char **av)
{
    // Register the plugin option
    spank_option_register(sp, &all_spank_options[0]);
    spank_option_register(sp, &all_spank_options[1]);
    spank_option_register(sp, &all_spank_options[2]);

    return 0;
}
/*
int slurm_spank_slurmd_exit(spank_t sp, int ac, char **av)
{
    if(spank_option_getopt(sp, &all_spank_options[0], NULL) == ESPANK_ERROR)
        return 0;

    if (spank_context() == S_CTX_SLURMD) {
        // Stopping the adopter process when slurmd is exiting
        // stop_adopter_process();
    }
    return 0;
}
*/



// Pro- und Epilog -> propably need spank_job_control_setenv to know which job to start/stop the adopter for
/*
int slurm_spank_job_prolog(spank_t sp, int ac, char **av) {
    FILE *log_file = fopen("/tmp/spank_prolog.log", "a");
    if (log_file != NULL) {
        fprintf(log_file, "Prolog called in context: %d\n", spank_context());

        fclose(log_file);
    }


    spank_err_t lo2s_option_set = spank_option_getopt(sp, &all_spank_options[0], NULL);

    if(lo2s_option_set != ESPANK_SUCCESS){
        return 0;
    }

    unsigned int job_id;
    spank_err_t rc_j = spank_get_item(sp, S_JOB_ID, &job_id);
    unsigned int node_id;
    spank_err_t rc_n = spank_get_item(sp, S_JOB_NODEID, &node_id);

    // Generate file-path for 
    char job_id_str[16];
    sprintf(job_id_str, "%d", job_id);

    char node_id_str[16];
    sprintf(node_id_str, "_%d", node_id);

    char **trace_path_pointer = NULL;
    char trace_path[128] = "";

    if(spank_option_getopt(sp, &all_spank_options[1], trace_path_pointer) == ESPANK_SUCCESS){
        strcat(trace_path, *trace_path_pointer);
    }else{
        getcwd(trace_path, sizeof(trace_path));
    }
    strcat(trace_path, "/lo2s_trace_job");
    strcat(trace_path, job_id_str);
    strcat(trace_path, node_id_str);

    char cgroup_path[256] = "";
    

    FILE *log_file = fopen("/tmp/spank_prolog.log", "a");
    if (log_file != NULL) {
        fprintf(log_file, "TOP: Option set: %s, %d\n", slurm_strerror(lo2s_option_set), lo2s_option_set);
        fprintf(log_file, "TOP: Jobinit: %s, J-ID:%d, N-ID:%d\n", slurm_strerror(rc_n), job_id, node_id);
        fprintf(log_file, "Generated trace path: %s\n", trace_path);

        fclose(log_file);
    }
    

    //init_adopter_process(trace_path, job_id_str);






        //spank_err_t env_is_set = spank_job_control_getenv(sp, "LO2s_VAR", env_val, sizeof(env_val));
        //fprintf(log_file, "TOP: ENV set: %s\n", slurm_strerror(env_is_set));

    
    return 0;
}


/*
int slurm_spank_job_epilog(spank_t sp, int ac, char **av) {
    fprintf(stderr, "lo2do job epilog called in context: %d\n", spank_context());
    
    unsigned int job_id;
    spank_err_t rc = spank_get_item(sp, S_JOB_ID, &job_id);

    
    //stop_adopter_process(job_id);

    return 0;
}
*/

// Is called in context local and remote
int slurm_spank_init_post_opt(spank_t sp, int ac, char **av)
{   
    if (spank_context() != S_CTX_REMOTE) return 0;

    unsigned int step_id;
    spank_get_item(sp, S_JOB_STEPID, &step_id);

    if (step_id != 0) return 0;

    unsigned int node_id;
    spank_get_item(sp, S_JOB_NODEID, &node_id);
    char node_id_str[16];
    sprintf(node_id_str, "_%d", node_id);

    unsigned int job_id;
    spank_get_item(sp, S_JOB_ID, &job_id);

    char job_id_str[16];
    sprintf(job_id_str, "%d", job_id);
      
    // GENERATE TRACEPATH    
    if(strcmp(lo2s_trace_path, "(null)") == 0){
        // TODO: lookup size-stuff
        getcwd(lo2s_trace_path, sizeof(lo2s_trace_path));
    }
    strcat(lo2s_trace_path, "/lo2s_trace_");
    strcat(lo2s_trace_path, job_id_str);
    strcat(lo2s_trace_path, node_id_str);

    
        
    
    return 0;
}


/*
int slurm_spank_local_user_init(spank_t sp, int ac, char **av)
{
    
    if (spank_option_getopt(sp, &all_spank_options[0], NULL) == ESPANK_SUCCESS) {
        //spank_setenv(sp, "LO2S_PLUGIN_ENABLED", "true", 256);
        fprintf(stderr, "lo2do enabled: %d\n", spank_context());
    }else {
        fprintf(stderr, "lo2do option not set, context: %d\n", spank_context());
    }

    return 0;
}
*/



int init_monitoring_process(char *trace_path) {
    pid_t pid = fork();
    if (pid == 0) {
        char *args[] = {"lo2s", "-o", trace_path, "-c", , NULL};
         
        if (execvp(args[0], args) < 0) {
            perror("Exec for lo2s_adopter failed");
            exit(1);
        }
    }else{
        usleep(500000);
    }
    return 0;
}

/*
int queue_step(int job_id, int argc, char** argv) {
    char job_id_str[16];
    sprintf(job_id_str, "%d", job_id);
    char argv_str[1024] = "";
    for (int i = 0; i < argc; i++) {
        strcat(argv_str, argv[i]);
        if (i < argc - 1) {
            strcat(argv_str, " ");
        }
    }
    char *args[] = {"lo2s_sender", job_id_str, argv_str, NULL};
        
    if (execvp(args[0], args) < 0) {
        perror("Exec for lo2s_step_queue failed");
        exit(1);
    }
    return 0;
}
*/
int stop_adopter_process(int job_id) {
    char job_id_str[16];
    sprintf(job_id_str, "%d", job_id);

    char *args[] = {"lo2s_sender", job_id_str, "exit_lo2s", NULL};
        
    if (execvp(args[0], args) < 0) {
        perror("Exec for lo2s_adopter closing failed");
        exit(1);
    }
    
}


