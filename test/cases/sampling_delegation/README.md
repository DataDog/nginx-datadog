These tests verify the behavior of `datadog_delegate_sampling` directive.

## `test_sampling_delegation.py`
![service diagram](diagrams/diagram.svg)

Each of the three nginx instances exposes endpoints that either do or don't
delegate sampling to the upstream. This test then examines the spans sent to the
agent (the bold arrows) to verify that the intended nginx instance made the
sampling decision.

## `test_add_header.py`
In order to send back the `X-Datadog-Trace-Sampling-Decision` response header,
the tracing module needs to use nginx's `add_header` directive. The `add_header`
directive is injected into the `http` block, so that it applies to all servers
and locations. However, if a configuration context (e.g. a `location`) contains
an `add_header` directive, then `add_header` directives in enclosing contexts
(e.g. `http`) are not inherited. This means that if a user uses `add_header`, it
discards to `add_header` injected by the tracing module.

To prevent this, the tracing module overrides the `add_header` directive to
include the sampling delegation response header in addition to whatever was
indicated by the user.

This test makes sure that response headers added via `add_header` do not affect
the appearance of the sampling delegation response header, and visa-versa.
