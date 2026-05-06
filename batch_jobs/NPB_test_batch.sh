#!/bin/bash
#SBATCH --nodes=1
#SBATCH --ntasks=4

source /etc/profile.d/lmod.sh
module load openmpi

srun --mpi=pmix /data/NPB3.4.2/NPB3.4-MPI/bin/lu.C.x