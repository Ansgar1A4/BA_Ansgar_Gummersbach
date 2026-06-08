#!/bin/bash

for i in {1..4}; do
    docker cp /home/ansgar/Dokumente/Bachelorarbeit/slurm-docker-cluster/plugins/lo2s-plugin/lo2s-helper/adopter.c slurm-cpu-worker-$i:/tmp/adopter.c
    docker cp /home/ansgar/Dokumente/Bachelorarbeit/slurm-docker-cluster/plugins/lo2s-plugin/lo2s-helper/sender.c slurm-cpu-worker-$i:/tmp/sender.c

    docker exec slurm-cpu-worker-$i bash -c "rm -f /usr/local/bin/lo2s_adopter && gcc -o /usr/local/bin/lo2s_adopter /tmp/adopter.c && rm -f /tmp/adopter.c"
    docker exec slurm-cpu-worker-$i bash -c "rm -f /usr/local/bin/lo2s_sender && gcc -o /usr/local/bin/lo2s_sender /tmp/sender.c && rm -f /tmp/sender.c"
done