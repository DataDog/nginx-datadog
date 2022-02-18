#pragma once

// `opentracing-cpp` places its symbols in the `::opentracing` namespace, while
// `dd-opentracing-cpp` places its symbols in the `::datadog::opentracing`
// namespace.  This project places its symbols in the `::datadog::nginx`
// namespace, and so referring to the namespace `opentracing` is ambiguous: do
// we mean `::datadog::opentracing` or `::opentracing`?  To resolve this
// ambiguity, we use the convention that namespace alias `ot` always refers
// to `::opentracing`.
namespace opentracing {}
namespace ot = ::opentracing;
