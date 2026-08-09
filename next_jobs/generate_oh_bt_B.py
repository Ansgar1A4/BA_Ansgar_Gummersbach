import os


template = """#!/bin/bash
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --output=overhead_ABC_traces/output_%j_B_{count}.txt
#SBATCH --mem=10240M
#SBATCH --lo2do=/data/overhead_ABC_traces
#SBATCH --lo2do_args="--count {count}"

source /etc/profile.d/lmod.sh
module load openmpi

srun --mpi=pmix /data/NPB3.4.2/NPB3.4-MPI/bin/bt.B.x
"""

# Schleife von 2 bis 9
count = 10
while count <= 1_000_000_000:
    # Multipliziere mit 1 Million
    count = count * 10
    
    # Fülle das Template mit der aktuellen Zahl
    script_content = template.format(count=count)
    
    # Definiere den Dateinamen
    filename = f"job_sp_B_{count}.sh"
    
    # Schreibe den Inhalt in die Datei
    with open(filename, "w") as f:
        f.write(script_content)
        
    print(f"Datei erstellt: {filename}")