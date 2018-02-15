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

docker run --detach --net dist_net -h "dme_bm" --name "dme_bm" dme_bm /bin/bm 

for nid in $(seq 1 $tot);
do
    host="dme-"$nid
    echo "Creating container with hostname: " $host
    #docker run --detach --net dist_net -h $host --name $host dme_nc "/bin/nc" $nid $tot "/lib/simple.so"
    docker run --detach --net dist_net -h $host --name $host dme_nc "/bin/nc" $nid $tot "/lib/ricart.so"
    # Sleeping in order to make sure container is listening before new container makes a call.
    sleep 1 
done
