#!/bin/bash
set -euo pipefail

export SRC_DIR=$(pwd)/src
export PLUGIN_DIR=/opt/lo2s-plugin
export PLUGSTACK_CONFIG=/usr/lib64/slurm/plugstack.conf
export PLUGIN_NAME=lo2do
worker_name=slurm-cpu-worker-
worker_count=4

cd ../../
./update_slurmfiles.sh
docker compose up -d

cd "${SRC_DIR}"
cd ..

docker exec slurmctld mkdir -p "${PLUGIN_DIR}"
docker cp "$(pwd)/plugstack.conf" slurmctld:"${PLUGSTACK_CONFIG}"

for n in {1..4}; do
    docker exec ${worker_name}${n} mkdir -p "${PLUGIN_DIR}"
    docker cp "$(pwd)/plugstack.conf" ${worker_name}${n}:"${PLUGSTACK_CONFIG}"
done

echo "Ensure PlugStackConfig is enabled in slurm.conf..."
docker exec slurmctld bash -lc "if grep -q '^PlugStackConfig' /etc/slurm/slurm.conf; then sed -i 's|^#*PlugStackConfig.*|PlugStackConfig=/usr/lib64/slurm/plugstack.conf|' /etc/slurm/slurm.conf; else echo 'PlugStackConfig=/usr/lib64/slurm/plugstack.conf' >> /etc/slurm/slurm.conf; fi"
for n in {1..4}; do
    docker exec ${worker_name}${n} bash -lc "if grep -q '^PlugStackConfig' /etc/slurm/slurm.conf; then sed -i 's|^#*PlugStackConfig.*|PlugStackConfig=/usr/lib64/slurm/plugstack.conf|' /etc/slurm/slurm.conf; else echo 'PlugStackConfig=/usr/lib64/slurm/plugstack.conf' >> /etc/slurm/slurm.conf; fi"
done

echo "Build plugin in slurmctld..."
docker exec -u root slurmctld bash -lc "gcc -shared -fPIC -I/usr/include/slurm -o '${PLUGIN_DIR}/${PLUGIN_NAME}.so' '${PLUGIN_DIR}/src/${PLUGIN_NAME}.c'"
docker exec -u root slurmctld chown slurm:slurm "${PLUGSTACK_CONFIG}" "${PLUGIN_DIR}/${PLUGIN_NAME}.so"

echo "Update Container"
for n in $(seq 1 ${worker_count}); do
    docker exec -u root ${worker_name}${n} chown slurm:slurm "${PLUGIN_DIR}/${PLUGIN_NAME}.so"
done

docker exec slurmctld scontrol reconfigure

