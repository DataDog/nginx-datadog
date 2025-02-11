# Global Assertions for RUM Auto-Injection

These integration tests are designed to validate the behavior of RUM Injector.
The tests have been created to be **agnostic**, meaning they can be reused across all web-server injectors
that support this functionality. This approach is similar to the methodology used in  [system-tests](https://github.com/DataDog/system-tests/tree/main),
which standardizes tests for APM tracers.

## Usage
Once the web-server under test is up and running, running the assertions should be straightforward.
For example:

```sh
pytest scenario
============================== test session starts ===============================
platform darwin -- Python 3.11.10, pytest-8.3.3, pluggy-1.5.0
rootdir: /Users/damien.mehala/workspace/inject-browser-sdk/tests/integration_tests
collected 6 items

scenario/test_injection.py ......                                          [100%]

=============================== 6 passed in 0.05s ================================
```

## Design
The key design goal is to allow the same set of tests to be shared across all web-server injectors.
This reuse introduces a level of flexibility but also increases complexity. We can draw a parallel 
to the [system-tests](https://github.com/DataDog/system-tests/tree/main) suite for APM Tracers, 
which shares this goal of generality. However, as anyone who has worked with system-tests will attest, 
the complexity of implementing new assertions or debugging the system can make it difficult to use, 
especially initially.

The purpose of this "framework" is to avoid repeating those mistakes by being deliberately opiniated.
These opinions, or assumptions, act as idioms within the framework. As long as these idioms are adhered to,
the execution of the integration test will be predictable and manageable.

### Assumptions
To ensure the tests can run on as many web-servers as possible, a set of strong assumptions has been made.
These assumptions allow us to keep the tests simple and consistent across all environments, but they also 
impose strict requirements on how the test environment is set up. 

The following assumptions **MUST** be met for the tests to run correctly:
- The web server with RUM Auto-Injection enabled is accessible at `localhost:8080`.
- A "vanilla" server (without RUM Auto-Injection) is accessible at `localhost:8081`.
- Both servers proxy requests to an upstream server located at `localhost:8090`.
- Both servers are started and fully operational before running the tests.

Importantly, **no orchestration is provided by these tests**. It is the user's responsibility to ensure that 
both servers are started and configured according to the assumptions above. The lack of orchestration means
that any issues in server setup **MUST** be resolved outside of the testing framework.

### Directory Structure
- `static/`: This directory contains static content that **MUST** be served by the web-server under test.
- `scenario/`: This directory holds the Python test cases that execute the integration tests against the 
configured servers.

