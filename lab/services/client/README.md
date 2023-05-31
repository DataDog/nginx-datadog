This directory contains the build instructions for the "client" service from
[docker-compose.yml](../../docker-compose.yml).

It contains command line tools that the test runner will use via
`docker-compose exec`, such as `grpcurl` and `curl`.  The test runner uses
these in-compose-container command line tools instead of using the network
directly, so that the test runner's network and the docker-compose network need
not be bridged.  We want to be able to run the tests in contexts where host
port binding is not allowed (such as CircleCI's in-docker docker-compose
offering).

`curljson.sh` is a wrapper around curl whose output is easy to parse.
