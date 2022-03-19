Integration Tests
=================
TODO:

- Python unittest module
    - each `test_*.py`, recursively down, is a test
- docker-compose
    - running up/down only once
    - nginx under test
    - mock agent
    - http service
    - fastcgi service
    - grpc service
- bin/run
- operations:
    - test nginx config
    - reload nginx to force it to send traces
    - "sync" the logs of a service, e.g. the agent
