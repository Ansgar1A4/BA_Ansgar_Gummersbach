USER=tester
for container in slurmctld slurm-cpu-worker-1 slurm-cpu-worker-2 slurm-cpu-worker-3 slurm-cpu-worker-4; do
  docker exec -u root "$container" bash -lc "id $USER >/dev/null 2>&1 || useradd -m -s /bin/bash $USER"
  docker exec -u root slurmctld bash -lc 'chown -R tester:tester /tmp && chmod -R u+rwX /tmp'
done

docker exec -u root slurmctld bash -lc "echo '$USER:secret123' | chpasswd"

docker exec -u root slurmctld bash -lc "sacctmgr add account cpu Description="CPU jobs" Organization=linux"
docker exec -u root slurmctld bash -lc "sacctmgr add user tester account=cpu"

docker exec -u root slurmctld bash -lc 'chown -R tester:tester /data /data/traces && chmod -R u+rwX /data /data/traces'