#!/bin/bash

for i in {1..4}; do
    docker cp /home/ansgar/Dokumente/Bachelorarbeit/slurm-docker-cluster/plugins/lo2s-plugin/lo2s-helper/lo2s_factory.c slurm-cpu-worker-$i:/tmp/lo2s_factory.c

    docker exec slurm-cpu-worker-$i bash -c "rm -f /usr/local/bin/lo2s_factory && gcc -o /usr/local/bin/lo2s_factory /tmp/lo2s_factory.c && rm -f /tmp/lo2s_factory.c"
done