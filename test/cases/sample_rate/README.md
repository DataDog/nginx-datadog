These tests verify the behavior of the `datadog_sample_rate` directive.

Behavior tested includes:

- `datadog_sample_rate` directive can appear in `http`, `server`, and `location`
  blocks, including in any combination. The "most specific" directive wins.
- If multiple directives are at the same level, then the first one that is "on"
  applies.
- The sample rate (first argument) must be a literal floating point number
  between 0.0 and 1.0 inclusive.
- Directives can appear on the same line (separated by a semicolon).
- If all directives at the lowest level are "off", then a directive at the next
  higher level applies.
- The matching directive is annotated in the span tag
  "nginx.sample_rate_source".
