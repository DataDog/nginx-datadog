This test verifies that the expected span tags are added to spans produced by
the nginx module.

Some tags are defined by default (see `TracingLibrary::default_tags` in the
module source), while others can be defined by the user via the `datadog_tag`
configuration directive.
