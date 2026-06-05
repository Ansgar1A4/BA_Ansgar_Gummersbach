#!/bin/bash

docker compose up -d

docker cp /home/ansgar/Dokumente/Bachelorarbeit/slurm-docker-cluster/plugins/lo2s-plugin/src/lo2do.c slurmctld:/tmp/lo2do.c
docker exec slurmctld bash -c "gcc -shared -fPIC -I/usr/include/slurm -o /usr/lib64/lo2do.so /tmp/lo2do.c"

docker cp slurmctld:/usr/lib64/lo2do.so ./lo2do.so

docker cp ./lo2do.so slurm-cpu-worker-1:/usr/lib64/lo2do.so
docker cp ./lo2do.so slurm-cpu-worker-2:/usr/lib64/lo2do.so
docker cp ./lo2do.so slurm-cpu-worker-3:/usr/lib64/lo2do.so
docker cp ./lo2do.so slurm-cpu-worker-4:/usr/lib64/lo2do.so

rm ./lo2do.so

docker exec slurmctld scontrol reconfigure

docker exec slurmctld bash -c 'scontrol update nodename=c[1-4] state=resume'
