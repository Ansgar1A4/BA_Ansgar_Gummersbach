#include <stddef.h>
#include <string.h>
#include <slurm/spank.h>

SPANK_PLUGIN(dummy, 1);

static char dummy_value[256] = "";

static int _opt_dummy_cb(int val, const char *optarg, int remote)
{
    if (optarg && *optarg) {
        strncpy(dummy_value, optarg, sizeof(dummy_value) - 1);
        dummy_value[sizeof(dummy_value) - 1] = '\0';
    }
    return 0;
}

struct spank_option spank_options[] = {
    {"dummy", "VALUE", "Set DUMMY_VAR when provided",
     1, 0, _opt_dummy_cb},
    SPANK_OPTIONS_TABLE_END
};

int slurm_spank_init(spank_t sp, int ac, char **av)
{
    if (spank_context() == S_CTX_REMOTE) {
        spank_option_register(sp, &spank_options[0]);
    }
    return 0;
}

int slurm_spank_task_init(spank_t sp, int ac, char **av)
{
    if (dummy_value[0]) {
        spank_setenv(sp, "DUMMY_VAR", dummy_value, 1);
    }
    return 0;
}





