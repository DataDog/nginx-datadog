#![allow(clippy::missing_safety_doc)]
// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache 2.0 License. This product includes software developed at
// Datadog (https://www.datadoghq.com/).
//
// Copyright 2024-Present Datadog, Inc.

use std::{ptr::null, slice};

use crate::{Snippet, SnippetInternal};
use inject_browser_sdk::injector;

/// A stateful object used to inject a Browser SDK [`Snippet`] inside a HTML document. Use
/// [`injector_create`] to create a new one.
///
/// Note: this struct is exposed as an FFI opaque pointer. Structure layout is irrelevant to the
/// library consumer as it is only allocated and accessed by the library.
pub struct Injector {
    // Use an internal [`injector::Injector`] with a static lifetime. Because this struct will be
    // used via a raw pointer, no lifetime can be guaranteed.
    inner: injector::Injector<'static>,
    // Store the last slices, so we keep ownership and return a pointer to the library consumer.
    slices_memory_storage: [BytesSlice; 4],
}

/// Create an [`Injector`] injector based on a [`Snippet`].
///
/// # Memory ownership
///
/// The `Injector` instance returned by this function is owned by the library and should be freed
/// by using [`injector_cleanup`] once done using it.
///
/// # Example
///
/// ```c
/// Injector* injector = injector_create(snippet);
///
/// // ... use injector
///
/// injector_cleanup(injector);
/// ```
#[no_mangle]
pub unsafe extern "C" fn injector_create(snippet: *const Snippet) -> *mut Injector {
    let snippet_content = match { SnippetInternal::borrow_public_ptr(snippet) }.inner {
        Ok(ref content) => content.as_slice(),
        Err(_) => &[],
    };

    return Box::into_raw(Box::new(Injector {
        inner: injector::Injector::new(snippet_content),
        slices_memory_storage: std::array::from_fn(|_| BytesSlice::empty()),
    }));
}

/// Free the [`Injector`] associated memory.
///
/// # Memory safety
///
/// Do not use the `injector` instance after calling this function.
#[no_mangle]
pub unsafe extern "C" fn injector_cleanup(injector: *mut Injector) {
    _ = Box::from_raw(injector);
}

/// Write part of an HTML document to the [`Injector`].
///
/// This function should not be called while another [`InjectorResult`] from the same `Injector` is
/// still being processed.
///
/// # Memory ownership
///
/// The library consumer owns the memory related to the `chunk` argument, and should free it once
/// it's not used anymore.
///
/// See [`InjectorResult`] documentation for details about the returned value memory ownership.
///
/// # Example
///
/// ```c
/// InjectorResult result = injector_write(injector, buffer, buffer_length);
/// for (int index = 0; index < result.slices_length; index += 1) {
///   BytesSlice slice = result.slices[index];
///
///   write(outgoing_stream, slice.start, slice.length);
/// }
/// ```
#[no_mangle]
pub unsafe extern "C" fn injector_write(
    injector: *mut Injector,
    chunk: *const u8,
    length: u32,
) -> InjectorResult {
    let ffi_injector = &mut *injector;
    let chunk = slice::from_raw_parts(chunk, length as usize);
    return InjectorResult::new(
        ffi_injector.inner.write(chunk),
        &mut ffi_injector.slices_memory_storage,
    );
}

/// Notify the [`Injector`] that the full HTML document has been written.
///
/// If the injection did not happen yet, a slice of the same size as the snippet containing only
/// white space will be returned to "pad" the response, so it has the same length as the announced
/// `content-length`.
///
/// This function should not be called while another [`InjectorResult`] from the same `Injector` is
/// still being processed.
///
/// # Memory ownership
///
/// See [`InjectorResult`] documentation for details about the returned value memory ownership.
///
/// # Example
///
/// ```c
/// InjectorResult result = injector_end(injector);
/// for (int index = 0; index < result.slices_length; index += 1) {
///   BytesSlice slice = result.slices[index];
///
///   write(outgoing_stream, slice.start, slice.length);
/// }
#[no_mangle]
pub unsafe extern "C" fn injector_end(ffi_injector: *mut Injector) -> InjectorResult {
    let ffi_injector = &mut *ffi_injector;
    return InjectorResult::new(
        ffi_injector.inner.end(),
        &mut ffi_injector.slices_memory_storage,
    );
}

/// The result of a call to [`injector_write`] or [`injector_end`]. It represents a list of binary
/// slices to be written to the outgoing stream.
///
/// In its current implementation, at most 4 slices can be returned. While rare, it is possible
/// that 0 slices are returned.
///
/// [`InjectorResult`] instances (and the memory they point to) should *not* be used anymore once
/// `injector_write` or `injector_end` are called with the same `Injector` instance.
///
/// # Memory ownership
///
/// `InjectorResult` is allocated on the stack. By itself it does not own any memory, but can
/// contain pointers to:
///
/// * portions of the chunk provided to [`injector_write`]. This chunk is owned by the library
/// consumer and should be freed once it is not used anymore.
///
/// * internal buffers owned by the [`Injector`] instance. Those buffers will be cleared by
/// [`injector_cleanup`].
#[repr(C)]
pub struct InjectorResult {
    /// Number of returned slices (any number from 0 to 4).
    pub slices_length: u32,

    /// A pointer to the first returned slice.
    pub slices: *const BytesSlice,

    /// Whether the injection occurred while processing the current chunk.
    pub injected: bool,
}

impl InjectorResult {
    fn new(result: injector::Result<'static>, slices_memory_storage: &mut [BytesSlice; 4]) -> Self {
        *slices_memory_storage = std::array::from_fn(|index| result.slices[index].into());
        Self {
            slices_length: result.length as u32,
            slices: slices_memory_storage as *const _,
            injected: result.injected,
        }
    }
}

/// A pointer to a sub-array of bytes, used within [`InjectorResult`].
///
/// # Memory ownership
///
/// See [`InjectorResult`].
#[repr(C)]
pub struct BytesSlice {
    /// Number of bytes.
    pub length: u32,

    /// A pointer to the first byte of the sub-array.
    pub start: *const u8,

    /// Whether the pointer refers to the chunk provided to the [`injector_write`] function. This
    /// is useful when using the library from a language providing safer ways to access memory than
    /// using raw pointers.
    ///
    /// By using this flag, one could avoid some unsafe pointer manipulation by computing the slice
    /// index relative to the start of the chunk, and use this index to access the memory-owned
    /// instance of the chunk.
    ///
    /// # Example (in "simplified" Rust as an illustration)
    ///
    /// ```ignore
    /// let chunk = b"foobar";
    /// let result = injector_write(injector, chunk.as_ptr(), chunk.len());
    /// for slice in result.slices {
    ///     if slice.from_incoming_chunk {
    ///         let start_index = slice.start - chunk.as_ptr();
    ///         let end_index = start_index + slice.end;
    ///         outgoing_stream.write(&chunk[start_index..end_index])
    ///     } else {
    ///         outgoing_stream.write(unsafe { std::slice::from_raw_parts(slice.start, slice.len) })
    ///     }
    /// }
    /// ```
    pub from_incoming_chunk: bool,
}

impl BytesSlice {
    fn empty() -> Self {
        Self {
            start: null(),
            length: 0,
            from_incoming_chunk: false,
        }
    }
}

impl<'a> From<injector::BytesSlice<'a>> for BytesSlice {
    fn from(slice: injector::BytesSlice<'a>) -> Self {
        Self {
            start: slice.bytes as *const _ as *const u8,
            length: slice.bytes.len() as u32,
            from_incoming_chunk: slice.from_incoming_chunk,
        }
    }
}
