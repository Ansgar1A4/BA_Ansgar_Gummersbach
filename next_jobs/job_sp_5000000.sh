#!/bin/bash
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --output=overhead_c8_n1_sp_5000000_traces/output_%j.txt
#SBATCH --mem=10240M
#SBATCH --lo2do=/data/overhead_c8_n1_sp_5000000_traces
#SBATCH --lo2do_args="--count 5000000"

source /etc/profile.d/lmod.sh
module load openmpi

srun --mpi=pmix /data/NPB3.4.2/NPB3.4-MPI/bin/bt.B.x
