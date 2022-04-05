These are the actual integration tests.

The tests run as python `unittest` test cases.  They share an instance of
`class Orchestration`, which encapsulates the `docker-compose` services
session by running `docker-compose up` before any tests begin and by running
`docker-compose down` after all tests have completed.

Usage
-----
The following examples are relative to the toplevel directory of the
repository.
```console
$ test/bin/run
......................................
----------------------------------------------------------------------
Ran 38 tests in 234.872s

OK
```

To see which tests are running, pass the `--verbose` flag.
```console
$ test/bin/run --verbose
test_auto_propagation (cases.auto_propagation.test_fastcgi.TestFastCGI) ... ok
test_disabled_at_http (cases.auto_propagation.test_fastcgi.TestFastCGI) ... ok
test_disabled_at_location (cases.auto_propagation.test_fastcgi.TestFastCGI) ... ok
test_disabled_at_server (cases.auto_propagation.test_fastcgi.TestFastCGI) ... ok
test_without_module (cases.auto_propagation.test_fastcgi.TestFastCGI) ... ok
Verify that the request span's operation name matches the default ... ok
[ ... etc. ... ]

----------------------------------------------------------------------
Ran 38 tests in 237.233s

OK
```

`test/bin/run` passes all of its arguments to the underlying call to `python3
-m unittest`, so you can run particular test cases, or customize the test run
in other ways.  See `test/bin/run --help`.

When running particular test cases, the package structure of the tests is
relevant.  For example, to run only the test
`test/cases/configuration/test_configuration.py`, the command would be:
```console
$ test/bin/run cases.configuration.test_configuration
```

To see very detailed output, tail the `logs/docker-compose-verbose.log` file.
```console
$ touch logs/docker-compose-verbose.log && tail -f logs/docker-compose-verbose.log &
$ test/bin/run
```

There are three ways to gather information about the result of a request sent
to nginx:

1. If we're testing whether tracing context is sent to a reverse proxied
   service, then the response delivered by the service contains the headers
   that it received, so those are sufficient to determine, e.g., whether a
   trace ID was propagated as `x-datadog-trace-id`.
2. If we're testing whether nginx produces a particular trace (set of spans)
   as a result of a request, then we examine the log output of the "agent"
   service.  The agent service prints to standard output a JSON representation
   of all traces it receives.
3. If we're testing behavior that is particular to the environment in which
   nginx is executed, then we run a temporary instance of nginx inside of the
   same container that is running the nginx service, and examine its output
   or responses that it delivers.  As of this writing, this is used only for
   testing nginx's environment variable configuration directives.

Each subdirectory is a category of tests, typically containing a single test
module `test_*.py`.  By convention, each subdirectory contains a `conf/`
subdirectory containing nginx configuration files that are used by the tests.

The `*.py` directly in this directory are common code shared by the tests.

- [case.py](case.py) is a wrapper around `unittest.TestCase` that provides an
  attribute `.orch` of type `Orchestration`.  Test cases can use this to share
  a single scoped session of `docker-compose` services.
- [formats.py](formats.py) contains parsing functions for the output of
  `docker-compose up`, `docker-compose down`, and the JSON-formatted
  traces logged by the agent service.
- [lazy_singleton.py](lazy_singleton.py) defines a generic singleton class,
  which is then used to define a single instance of `Orchestration`, the
  `docker-compose` wrapper.
- [orchestration.py](orchestration.py) defines a `class Orchestration` that
   manages a thread that runs and consumes the output of `docker-compose up`,
   and has methods for performing operations on the `docker-compose` setup,
   e.g.
   - sending a request to nginx,
   - retrieving the logs of a service,
   - establishing a synchronization point in the logs of a service,
   - replacing nginx's configuration,
   - reloading nginx (to accept a new config, or to force it to flush traces),
   - validating an nginx configuration,
   - starting a temporary instance of nginx.
