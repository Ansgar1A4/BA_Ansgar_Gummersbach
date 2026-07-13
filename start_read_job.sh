#!/bin/bash

for i in {1..4}; do
    docker exec slurm-cpu-worker-$i rm -f /tmp/spank_prolog.log
done

docker exec slurmctld bash -c 'sbatch do_lo2s_test.sh'
docker exec slurmctld bash -c 'rm -rf lo2s_trace_*'

sleep 2

for i in {1..4}; do
    echo "Logs from worker $i:"
    docker exec slurm-cpu-worker-$i cat /tmp/spank_prolog.log
done

