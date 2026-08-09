#!/bin/bash
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --output=overhead_ABC_traces/output_%j_C_10000.txt
#SBATCH --mem=10240M
#SBATCH --lo2do=/data/overhead_ABC_traces
#SBATCH --lo2do_args="--count 10000"

source /etc/profile.d/lmod.sh
module load openmpi

srun --mpi=pmix /data/NPB3.4.2/NPB3.4-MPI/bin/bt.C.x
