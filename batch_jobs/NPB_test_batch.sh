#!/bin/bash
#SBATCH --nodes=4
#SBATCH --cpus-per-task=4
#SBATCH --output=output%j.txt

source /etc/profile.d/lmod.sh
module load openmpi

srun --mpi=pmix /data/NPB3.4.2/NPB3.4-MPI/bin/lu.C.x
