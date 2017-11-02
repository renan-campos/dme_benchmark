#
# A simple script to run node controllers for quick testing.
#
#

if [ $# -lt 2 ]
then
echo "USAGE: $ bash run.sh <node-id> <total-nodes>"
exit 1
fi

nid=$1
tot=$2
host="dme-"$nid

# Creates user defined network so that hostnames automatically work.
docker network inspect dist_net &> /dev/null || docker network create dist_net

echo "Creating container with hostname: " $host
docker run --detach --net dist_net -h $host --name $host dist_mut_exc /bin/nc $nid $tot
