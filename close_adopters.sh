#!/bin/bash

for i in {1..4}; do
    docker exec slurm-cpu-worker-$i lo2s_sender $1 exit_lo2s
done



