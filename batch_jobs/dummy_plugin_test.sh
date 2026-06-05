#!/bin/bash
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --time=00:10:00
#SBATCH --output=t1.out
#SBATCH --dummy=5


srun echo "DUMMY_VALUE is $DUMMY_VAR, should be 1"
srun --dummy=1 bash -c 'echo "DUMMY_VALUE is $DUMMY_VAR, should be 1"'


