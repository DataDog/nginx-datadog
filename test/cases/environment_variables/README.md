This test verifies that loading the Datadog module causes certain environment
variables to be forwarded to nginx worker processes.

The idea is that certain tracer configuration, such as the service "env," the
URL of the trace agent, the sampling rate, etc. needs to be specified as
environment variables when nginx is launched.  However, the launched nginx is
not the process in which the tracer runs.  Instead, nginx spawns worker
processes, and they are the processes that handle requests and generate spans.
The problem is, nginx does not forward its environment to worker processes.
Each desired environment variable must be named in the configuration file via
the `env` directive.

The Datadog module is supposed to do the work of these `env` directives without
the user having to specify them in nginx's configuration file.

This test operates by running a second "master" instance of nginx within the
nginx service container.  This new nginx uses a different configuration, log
file, port, and additionally has environment variables set.  The test then
sends a request to this nginx, which responds with its environment.  The test
then verifies that the reported environment variables are as expected.
