#!/bin/bash

for trace in $(docker exec slurmctld bash -c "ls /data"); do
    if [[ $trace == lo2s_trace_* ]]; then
        docker cp slurmctld:/data/$trace saved_traces/$trace
    fi
done

