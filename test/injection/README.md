# Nginx injection acceptance tests

These tests install the real Datadog injector and language packages into a disposable
Linux host. They check HTTP responses, decoded spans, and Nginx → Flask parent links.
The same cases run against host processes, a Debian Nginx container, and an Alpine
Nginx container. Kubernetes is outside this suite. Stable-config tracing cases are
strict expected failures until the C++ tracer supports the feature.

## Run

Install Docker and `uv`, then run from the repository root:

```sh
DOCKER_CONTEXT=orbstack make test-injection INJECTION_SOURCE=published
DOCKER_CONTEXT=orbstack make test-injection INJECTION_SOURCE=checkout
make test-injection INJECTION_PACKAGE=/path/to/package.tar
```

The Docker daemon must support privileged Linux containers. Orb uses its native ARM64
architecture. `DOCKER_CONTEXT` and `DOCKER_HOST` also work with remote daemons; the
harness transfers files with Docker copy/exec and streams images with save/load.
It never needs a local bind mount or access to a remote daemon's published ports.
`INJECTION_ARCH=arm64` or `amd64` enforces the expected daemon architecture.

Published packages need access to `install.datadoghq.com`, Docker Hub, and GHCR.
Checkout builds also need access to the repository's private toolchain and packaging
images on `registry.ddbuild.io`. The checkout command initializes submodules, runs
the existing musl build with `RUM=ON`, and uses `datadog-package create` with the same
`nginx/<version>/ngx_http_datadog_module.so` layout as `package-oci`. An explicit
package takes precedence over `INJECTION_SOURCE`. Its Linux architecture, OCI blob
digests, and installed module checksum must match.

To investigate one case:

```sh
DOCKER_CONTEXT=orbstack make test-injection \
  INJECTION_TEST_ARGS='--injection-mode docker-alpine -k test_proxy_trace_link -x' \
  INJECTION_ARTIFACTS=test/injection/artifacts/debug
```

Tests run serially. Do not use pytest-xdist. A full run covers all three modes;
filtering is for local diagnosis. Skipped cases and unexpected passes fail the run.
Stable-config cases are marked as strict xfails. Positive conditions have a
30-second deadline. Negative checks follow graceful workload shutdown and observe
a three-second quiet period while checking collector health. Readiness uses
`/ready`; assertions match unique request URIs.

## What gets installed

| Component | Pin |
| --- | --- |
| Nginx | 1.31.5 |
| Docker binaries in the private host | 27.3.1 |
| Injector | 0.71.0-1 |
| Published Nginx library | 1.23.0-1 |
| Python library | 4.14.0-1 |
| APM test agent | v1.65.0 |
| Flask / Gunicorn | 3.1.2 / 23.0.0 |
| pytest | 8.4.2 |

The installer bootstrap uses Datadog's install-only pattern with agent installation
disabled. Language packages and the pinned injector are installed with the installer's
package commands. Host tests require the resulting `/etc/ld.so.preload`; Docker
tests require the running private daemon's default runtime to be `dd-shim`.
Workload containers specify no runtime. Collectors explicitly use `runc`, have
their own readiness checks, and do not forward data.

The application has no tracing imports, tracing launcher, manual preload, or Datadog
Nginx directives. Test settings reach Nginx through environment variables at master
startup, including in the environment-tags and reload cases.

## Examples

Both examples use the acceptance harness and keep the disposable host running:

```sh
DOCKER_CONTEXT=orbstack make injection-example-up INJECTION_MODE=host
make injection-example-request
make injection-example-traces
make injection-example-down

DOCKER_CONTEXT=orbstack make injection-example-up INJECTION_MODE=docker
make injection-example-request
make injection-example-traces
make injection-example-down
```

`docker-alpine` selects Alpine. `INJECTION_SOURCE=checkout` and `INJECTION_PACKAGE`
work for examples too. The state file records the Docker context, so later commands
use the same daemon. Keep `INJECTION_ARTIFACTS` consistent between commands.
Requests run through `docker exec` and return the backend's echoed propagation
headers. The traces command prints the test agent's decoded traces. Shutdown flushes
the workloads and saves diagnostics before removing the example resources.

Set these variables on `injection-example-up` to change the Nginx environment:

| Setting | Example |
| --- | --- |
| Trace agent URL | `DD_TRACE_AGENT_URL=http://127.0.0.1:8126` |
| Host and nondefault port | `DD_AGENT_HOST=127.0.0.1 DD_TRACE_AGENT_PORT=9126` |
| Unix socket | `DD_TRACE_AGENT_URL=unix:///var/run/datadog/apm.socket` |
| Service and version tags | `DD_SERVICE=frontend DD_ENV=dev DD_VERSION=example` |
| Extra tags | `DD_TAGS=team:nginx,example:injection` |
| Sampling rule | `DD_TRACE_SAMPLING_RULES='[{"service":"injection-nginx","sample_rate":0}]'` |
| W3C propagation | `DD_TRACE_PROPAGATION_STYLE_EXTRACT=tracecontext DD_TRACE_PROPAGATION_STYLE_INJECT=tracecontext` |
| Disable Nginx reporting | `DD_TRACE_ENABLED=false` |
| Opt Nginx out of injection | `DD_INSTRUMENT_SERVICE_WITH_APM=false` |

`DD_TRACE_AGENT_URL` takes precedence over host/port. Collector A listens on 8126
and the Unix socket; collector B listens on 9126. These addresses are inside the
disposable host. Flask remains traced when Nginx reporting or injection is disabled.
The sampling test checks the backend's received priority of `-1`; absence of raw
traces alone does not establish a sampling decision.

Nginx captures supported Datadog environment variables when its master starts and
restores them in workers. No Nginx `env` directives are needed. Changing those
values requires restarting the master or recreating the container; a graceful
configuration reload retains the original values. This is separate from the
official image's Nginx configuration templating and from Datadog stable config.

## Stable config

The suite writes stable configuration to both supported locations:

- `/etc/datadog-agent/application_monitoring.yaml`
- `/etc/datadog-agent/managed/datadog-agent/stable/application_monitoring.yaml`

The Docker injector mounts existing files at those paths into ordinary workload
containers. Tests assert that the file is visible to Nginx before checking default
configuration, managed > environment > local precedence, environment-variable
targeting rules, service tags, and tracing disablement. These cases follow the
system-tests YAML shapes for `apm_configuration_default` and
`apm_configuration_rules`.

Each case uses `xfail(strict=True)`. The current missing C++ tracer feature is an
expected failure. When support lands, an unexpected pass fails CI and requires
removing the marker.

## Evidence and CI

Artifacts default to `test/injection/artifacts/latest/`. They include JUnit,
resolved image IDs/digests, package manifest digests, installer version/output,
module checksums, daemon configuration, process maps, workload logs, injection
telemetry, request responses, and decoded traces from both collectors. Missing
required evidence fails the run. Failure cleanup also copies raw daemon/workload
logs. The suite removes only its own containers, Docker data volumes, and generated
image tags; it never prunes the daemon.

`.gitlab/injection-tests.yml` extends the existing `.test` runner setup. Checkout
jobs consume the architecture-matched Linux `package-oci` artifact on branches
and tags. Published jobs run on schedules and are also available manually.
Both architectures run all modes serially, with a 30-minute timeout, infrastructure
retries, and artifacts published even on failure.

Release acceptance requires full published and checkout runs on native ARM64 Orb,
both architectures in GitLab, and saved evidence of a linked Nginx → Flask trace.
Test collection or a filtered smoke run does not meet that requirement.
