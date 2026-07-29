#!/bin/bash
#SBATCH --nodes=4
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=CPU_COUNT
#SBATCH --output=outputSAMPLE_PERIOD.txt
#SBATCH --mem=2048M
#SBATCH --lo2do=/data/traces/TRACE_FILE
#SBATCH --lo2do_args="-c SAMPLE_PERIOD"
source /etc/profile.d/lmod.sh
module load openmpi

srun --mpi=pmix /data/NPB3.4.2/NPB3.4-MPI/bin/lu.B.x





