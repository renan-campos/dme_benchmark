#
# Automatically starts the specified number of nodes in order.
#

if [ $# -lt 1 ]
then
echo "USAGE: $ bash start.sh <total-nodes>"
exit 1
fi

tot=$1

# Creates user defined network so that hostnames automatically work.
docker network inspect dist_net &> /dev/null || docker network create dist_net

for nid in $(seq 1 $tot);
do
    host="dme-"$nid
    echo "Creating container with hostname: " $host
    docker run --detach --net dist_net -h $host --name $host dist_mut_exc /bin/nc $nid $tot
    # Sleeping in order to make sure container is listening before new container makes a call.
    sleep 1 
done
