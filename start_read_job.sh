#!/bin/bash

for i in {1..4}; do
    docker exec slurm-cpu-worker-$i rm -f /tmp/spank_lo2do.log
done

docker exec slurmctld bash -c 'rm -rf traces/lo2s_trace_*'
docker exec slurmctld bash -c 'sbatch do_lo2s_test.sh'
sleep 2

for i in {1..4}; do
    echo "\n\nLogs from worker $i:"
    docker exec slurm-cpu-worker-$i cat /tmp/spank_lo2do.log
done

