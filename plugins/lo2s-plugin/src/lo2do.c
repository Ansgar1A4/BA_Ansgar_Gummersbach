#include <stddef.h>
#include <string.h>
#include <slurm/spank.h>
#include <stdio.h>
#include <stdlib.h>

SPANK_PLUGIN(lo2do, 1);

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
    //fprintf(stderr, "lo2do init called in context: %d, enabled: %s\n", spank_context(), getenv("SPANK_LO2S_PLUGIN_ENABLED"));

    return 0;
}

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

int slurm_spank_job_prolog(spank_t sp, int ac, char **av)
{   
    FILE *log_file = fopen("/tmp/spank_prolog.log", "w");
    char buffer[256] = "";
    if (log_file != NULL) {
        unsigned int node_id;
        spank_err_t rc = spank_get_item(sp, S_JOB_NNODES, &node_id);
        //spank_err_t err = spank_getenv(sp, "LO2S_PLUGIN_ENABLED", buffer, sizeof(buffer));
        //fprintf(log_file, "lo2do job prolog called in context: %d\n , ret-value: %s", spank_context(), err == ESPANK_SUCCESS ? buffer : spank_strerror(err));
        fprintf(log_file, "lo2do job prolog called in context: %d, ID: %d, err: %s\n", spank_context(), node_id, spank_strerror(rc));
        fclose(log_file);
    } 
    
    return 0;
}

