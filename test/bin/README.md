These are scripts that are useful when working with the integration tests.

- [format](format) uses `yapf3` to format all of the Python code.
- [run](run) is a wrapper around `python3 -m unittest` that finds all test
  cases and runs them.  It also forwards its arguments to `python3 -m
  unittest`, so for example you can run a subset of tests, or increase the
  verbosity of logging.
- [run_parallel](run_parallel) is a wrapper around [run](run) that executes
  each test Python module in its own docker-compose "project," all in parallel.
