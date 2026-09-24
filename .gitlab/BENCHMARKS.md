# Benchmarks

GitLab CI configuration for the benchmarks that run on the
[Benchmarking Platform](https://datadoghq.atlassian.net/wiki/spaces/APMINT/pages/2419261562/Benchmarking+Platform).

## Layout

- `benchmarks.yml`: k6 load tests against an Nginx server with the Datadog module.
    - `baseline` and `only-tracing` run the `normal_operation` and `high_load` scenarios via the
      `.benchmarks` template.
    - `check-slo-breaches` gates releases based on SLOs defined on `bp-runner.fail-on-breach.yml`.
    - Steps live in the `cpp/nginx` branch of
      [benchmarking-platform](https://github.com/DataDog/benchmarking-platform).

## Marking a benchmark as flaky

Add it to `FLAKY_BENCHMARKS_REGEX` in `.benchmarks` in `benchmarks.yml`.

The benchmark still runs and reports, but doesn't fail the gate.

- The regex matches anywhere in the scenario name.
    - `normal_operation` quarantines every `normal_operation` scenario, across both
      `baseline` and `only-tracing`.
    - Anchor with `^...$` to target one scenario.

```yaml
FLAKY_BENCHMARKS_REGEX: "^high_load--only-tracing--nginx-utilization$"
```

Open a ticket to fix or remove it. See
[Flaky Benchmarks Monitoring](https://datadoghq.atlassian.net/wiki/spaces/APMINT/pages/7223313012/Flaky+Benchmarks+Monitoring).
