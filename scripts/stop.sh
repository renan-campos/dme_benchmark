#
# Stops containers
# WARNING: This stops and removes ALL containers. 
#
docker stop $(docker ps -q)

docker rm $(docker ps -a -q)
