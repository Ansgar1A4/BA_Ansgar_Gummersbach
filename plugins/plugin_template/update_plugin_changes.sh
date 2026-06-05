#!/bin/bash

../update_slurmfiles.sh
docker compose  up -d

export SRC_DIR=$(pwd)/src
export TARGET_DIR=/usr/lib64/slurm
export PLUGIN_DIR=${TARGET_DIR}
export PLUGSTACK_CONFIG=${TARGET_DIR}/plugstack.conf
export PLUGIN_NAME=dummy_plugin
worker_list="slurm-cpu-worker-1 slurm-cpu-worker-2 slurm-cpu-worker-3 slurm-cpu-worker-4"

for container in slurmctld $worker_list; do
    docker exec $container mkdir -p ${PLUGIN_DIR}
    docker cp ${SRC_DIR}/plugstack.conf $container:${PLUGSTACK_CONFIG}
done

echo "Build plugin in slurmctld..."
docker cp ${SRC_DIR}/${PLUGIN_NAME}.c slurmctld:/tmp/${PLUGIN_NAME}.c
docker exec -u root slurmctld gcc -shared -fPIC -I/usr/include/slurm -o ${PLUGIN_DIR}/${PLUGIN_NAME}.so /tmp/${PLUGIN_NAME}.c

echo "Update Container"
for c in $worker_list; do
    docker exec slurmctld cat ${PLUGIN_DIR}/${PLUGIN_NAME}.so | docker exec -i $c sh -c "cat > ${PLUGIN_DIR}/${PLUGIN_NAME}.so"
done

docker exec slurmctld scontrol reconfigure

