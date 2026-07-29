for n in {1..4}; do
    docker cp /home/ansgar/Dokumente/Bachelorarbeit/slurm-docker-cluster/plugins/lo2s-plugin/plugstack.conf slurm-cpu-worker-${n}:/etc/slurm/plugstack.conf 
done

docker cp /home/ansgar/Dokumente/Bachelorarbeit/slurm-docker-cluster/plugins/lo2s-plugin/plugstack.conf slurmctld:/etc/slurm/plugstack.conf 

docker exec slurmctld bash -c 'scontrol reconfigure'
