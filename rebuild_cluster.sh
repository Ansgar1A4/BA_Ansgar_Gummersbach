#!/bin/sh

make rebuild
make up


export BATCH_DIR=/data/batch_jobs

docker exec slurmctld mkdir -p $BATCH_DIR

echo "Add batch jobs to cluster"
for file in batch_jobs/*.sh; do
    docker cp $file slurmctld:$BATCH_DIR/$(basename $file)
    docker exec -u root slurmctld chmod +x $BATCH_DIR/$(basename $file)
done

echo "Add plugin to cluster"
cd plugin_template
./update_plugin_changes.sh


echo "Download benchmarks"
docker exec slurmctld bash -c "wget -qO- https://www.nas.nasa.gov/assets/npb/NPB3.4.2.tar.gz | tar xzvf - -C /data"
docker cp npbMPI_make.def slurmctld:/data/NPB3.4.2/NPB3.4-MPI/config/make.def

echo "Install OpenMPI and build benchmarks with it"
docker exec slurmctld bash -lc 'source /etc/profile.d/lmod.sh && spack install openmpi@5.0.8'
docker exec -u root slurmctld bash -c "cd /data/NPB3.4.2/NPB3.4-MPI/ && 
        module load openmpi &&
        make \"lu\" CLASS=\"C\""

