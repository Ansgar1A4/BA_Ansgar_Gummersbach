#!/bin/bash

for i in {1..1}; do
    docker cp /home/ansgar/Dokumente/Bachelorarbeit/slurm-docker-cluster/plugins/lo2s-plugin/lo2s-helper/lo2d.c slurm-cpu-worker-$i:/tmp/lo2d.c

    docker exec slurm-cpu-worker-$i bash -c "rm -f /usr/local/bin/lo2d && gcc -o /usr/local/bin/lo2d /tmp/lo2d.c && rm -f /tmp/lo2d.c"
done