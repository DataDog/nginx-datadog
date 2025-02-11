// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache 2.0 License. This product includes software developed at
// Datadog (https://www.datadoghq.com/).
//
// Copyright 2024-Present Datadog, Inc.

//! A library to inject the Datadog Browser SDK inside an HTML document. This library is used by
//! Web servers modules and APM tracer libraries to allow customers to enable Real User Monitoring
//! easily, without modifying their Web application.
//!
//! ## Build
//!
//! Build the library by using `cargo build --release`. The resulting artifact should be located
//! in:
//!
//! | Platform | Path                                       |
//! |----------|--------------------------------------------|
//! | Windows  | `target/release/injectbrowsersdk.dll`      |
//! | Linux    | `target/release/libinjectbrowsersdk.so`    |
//! | MacOS    | `target/release/libinjectbrowsersdk.dylib` |
//!
//! ## Usage overview
//!
//! This library has two mains concepts:
//!
//! * [`Snippet`], created by [`snippet_create_from_json`] by providing a configuration
//! formatted as JSON. `Snippet` are generally created once at the start of the process. Multiple
//! instances can be created if multiple configuration are being used.
//!
//! * [`Injector`], created by [`injector_create`] by providing a `Snippet` instance. One
//! [Injector] instance is usually created for each HTTP response where we want to inject the
//! snippet.
//!
//! A typical usage of this library consists in the following steps:
//!
//! 1. Gather the SDK configuration values provided by the customer (ex: passed as environment
//!    variables, configuration file…)
//!
//! 2. Call [`snippet_create_from_json`] to create a [`Snippet`] instance based on the
//!    configuration.
//!
//! 3. Ensure the snippet creation succeeded by checking the [`Snippet::error_code`]. If it is
//!    different than `0`, skip the injection altogether. A user-friendly error message is provided
//!    in [`Snippet::error_message`].
//!
//! 4. On each request, do:
//!
//!     1. If the library consumer forwards the request to an upstream service, set the **request**
//!        header `x-datadog-rum-injection-pending: 1` to indicate that the injection is pending,
//!        and the upstream service should avoid performing the injection.
//!
//!     2. Detect if the snippet has already been injected in the HTML document by checking
//!        whether the **response** header `x-datadog-rum-injected: 1` is present. If it is, skip
//!        the injection.
//!
//!     3. Detect if the response body is actually HTML by checking whether the response header
//!        `content-type` is `text/html`. If it is not HTML, skip the injection.
//!
//!     4. Update the response `content-length` header by incrementing it by [`Snippet::length`].
//!
//!     5. Call [`injector_create`] to create an [`Injector`] instance based on the `Snippet`.
//!
//!     6. For each binary chunk composing the HTTP response:
//!
//!         1. Call [`injector_write`], and write the returned slices to the outgoing stream.
//!
//!         2. If [`InjectorResult::injected`] is `true`, you can immediately write the rest of the
//!            HTTP response chunk to the outgoing stream.
//!
//!     7. If [`InjectorResult::injected`] was never `true`, call [`injector_end`] and write the
//!        returned slices to the outgoing stream.
//!
//!     8. Call [`injector_cleanup`] to free the `Injector` instance.
//!
//!
//! 5. Call [`snippet_cleanup`] to free the `Snippet` instance.
//!
//!

#![warn(missing_docs)]
#![allow(deprecated)] // allow internal usage of deprecated things
mod injector;
mod snippet;

pub use injector::*;
pub use snippet::*;
