This directory contains the build instructions for the "nginx" service from
[docker-compose.yml](../../docker-compose.yml).  It's based on a specified
nginx docker image, and then has the Datadog module installation added to it,
along with some utilities that the integration tests `docker-compose exec`
within the container (such as `ps`).