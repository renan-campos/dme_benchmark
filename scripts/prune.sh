#
# Frees up unused resources being held by docker.
#

# This will remove all stopped containers
docker container prune

# This will remove all unused volumes
docker volume prune

# This will remove all unused networks
docker network prune
