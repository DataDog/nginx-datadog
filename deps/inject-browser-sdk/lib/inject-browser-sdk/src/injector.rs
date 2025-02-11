// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache 2.0 License. This product includes software developed at
// Datadog (https://www.datadoghq.com/).
//
// Copyright 2024-Present Datadog, Inc.

use std::mem;

use crate::injection_point_locator::{InjectionPointLocator, Location};

/// Stateful object used to inject the provided snippet within a streamed HTML document.
pub struct Injector<'snippet> {
    snippet: &'snippet [u8],
    locator: InjectionPointLocator,
    injected: bool,

    /// When writing chunks progressively, some data might be missing to locate the injection
    /// point.
    ///
    /// There are two buffers here, because in some cases we need to return previously buffered
    /// data *and* buffer part of the chunk (see second example). Once emitted, the buffer needs to
    /// be emptied, so we don't emit the same data twice. To keep things simple, as a convention,
    /// we only write in `incomplete_data` and move it to the exposed `incomplete_data_public_ref`
    /// buffer when needed.
    ///
    /// Example: imagine we write the following chunks: `foo</he`, `ad>bar`.
    /// * When writing the first chunk, the injector will return `foo`, but buffer `</he`, as the
    /// snippet might be injected after `foo`.
    /// * When writing the second chunk, the injector will realize that `</he` was indeed the
    /// beginning of the `</head>` tag, so it will emit the snippet, then the buffered data `</he`,
    /// then the chunk `ad>bar`.
    ///
    /// Second example: imagine we write the following chunks: `foo</he`, `ader>bar`.
    /// * Same as before, the injector will return `foo` and buffer `</he`
    /// * When writing the second chunk, the injector will realize that `</he` was not the start of
    /// a `</head>` tag, so it will emit the buffered data `</he`, then the chunk `ader>bar`.
    ///
    /// Third example: imagine we write the following chunks: `foo</he`, `ader>bar</`
    /// * Same as before, the injector will return `foo` and buffer `</he`
    /// * When writing the second chunk, the injector will realize that `</he` was not the start of
    /// a `</head>` tag, so it will emit it, then emit `ader>bar`. But it will need to put `</` in
    /// a second buffer, as it cannot tell yet if it is a valid injection point.
    incomplete_data: Vec<u8>,
    incomplete_data_public_ref: Vec<u8>,

    /// If we don't find any injection point, we need to pad the document with spaces, so the
    /// document has the same size as if the snippet was successfully injected.
    padding_spaces: Vec<u8>,
}

impl<'snippet> Injector<'snippet> {
    pub fn new(snippet: &'snippet [u8]) -> Self {
        Self {
            snippet,
            locator: InjectionPointLocator::new(),
            injected: false,

            padding_spaces: Vec::new(),
            incomplete_data: Vec::new(),
            incomplete_data_public_ref: Vec::new(),
        }
    }

    pub fn write<'a>(&'a mut self, chunk: &'a [u8]) -> Result<'a> {
        if self.injected {
            return self.create_result(&[chunk]);
        }

        match self.locator.scan(chunk) {
            Location::None => {
                self.incomplete_data_public_ref = mem::take(&mut self.incomplete_data);
                return self.create_result(&[&self.incomplete_data_public_ref, chunk]);
            }

            Location::PotentialFromPreviousChunk => {
                self.incomplete_data.extend_from_slice(chunk);
                return self.create_result(&[]);
            }

            Location::PotentialFromIndex(index) => {
                self.incomplete_data_public_ref = mem::take(&mut self.incomplete_data);
                self.incomplete_data.extend_from_slice(&chunk[index..]);
                return self.create_result(&[&self.incomplete_data_public_ref, &chunk[..index]]);
            }

            Location::MatchFromPreviousChunk => {
                self.incomplete_data_public_ref = mem::take(&mut self.incomplete_data);
                self.injected = true;
                return self.create_result(&[
                    self.snippet,
                    &self.incomplete_data_public_ref,
                    chunk,
                ]);
            }

            Location::MatchFromIndex(index) => {
                self.incomplete_data_public_ref = mem::take(&mut self.incomplete_data);
                self.injected = true;
                return self.create_result(&[
                    &self.incomplete_data_public_ref,
                    &chunk[..index],
                    self.snippet,
                    &chunk[index..],
                ]);
            }
        }
    }

    pub fn end(&mut self) -> Result<'_> {
        self.incomplete_data_public_ref = mem::take(&mut self.incomplete_data);
        if self.injected {
            return self.create_result(&[&self.incomplete_data_public_ref]);
        } else {
            self.padding_spaces = vec![b' '; self.snippet.len()];
            return self.create_result(&[&self.incomplete_data_public_ref, &self.padding_spaces]);
        }
    }

    fn create_result<'a>(&'a self, slices: &[&'a [u8]]) -> Result<'a> {
        let mut length = 0;
        let mut non_empty_slices = slices.iter().filter(|slice| !slice.is_empty());

        Result {
            slices: std::array::from_fn(|_| match non_empty_slices.next() {
                Some(slice) => {
                    length += 1;
                    BytesSlice {
                        bytes: slice,
                        from_incoming_chunk: slice.as_ptr()
                            != self.incomplete_data_public_ref.as_ptr()
                            && slice.as_ptr() != self.padding_spaces.as_ptr()
                            && slice.as_ptr() != self.snippet.as_ptr(),
                    }
                }
                None => BytesSlice::default(),
            }),
            length,
            injected: self.injected,
        }
    }
}

#[derive(Default, Copy, Clone)]
pub struct BytesSlice<'a> {
    pub bytes: &'a [u8],
    pub from_incoming_chunk: bool,
}

/// Stack allocated "array" of at most 4 references to byte slices.
pub struct Result<'a> {
    pub slices: [BytesSlice<'a>; 4],
    pub length: usize,
    pub injected: bool,
}

impl<'a> Result<'a> {
    #[cfg(test)]
    fn iter(&self) -> impl Iterator<Item = BytesSlice<'_>> {
        return self.slices[..self.length].iter().copied();
    }
}

#[cfg(test)]
mod tests {
    use pretty_assertions::assert_eq;
    use rand::{distributions::Uniform, prelude::Distribution, seq::SliceRandom, Rng};

    use super::*;

    #[test]
    fn injector_basic() {
        test_injector(["abc</head>def"], "abc<snippet></head>def");
        test_injector(["abc</he", "ad>def"], "abc<snippet></head>def");
        test_injector(["abc", "</head>def"], "abc<snippet></head>def");
        test_injector(["abc</head>", "def"], "abc<snippet></head>def");
        test_injector(["abc</h", "ea", "d>def"], "abc<snippet></head>def");
        test_injector(["abc", "</hea", "d>def"], "abc<snippet></head>def");
    }

    #[test]
    fn no_head() {
        test_injector(["abc"], "abc         ");
        test_injector(["abc</hea"], "abc</hea         ");
    }

    #[test]
    fn empty() {
        test_injector::<&str>([], "         ");
        test_injector([""], "         ");
        test_injector(["", ""], "         ");
    }

    #[test]
    fn multiple_head() {
        test_injector(
            ["abc</head>def</head>ghi"],
            "abc<snippet></head>def</head>ghi",
        );
        test_injector(
            ["abc</head>def</h", "ead>ghi"],
            "abc<snippet></head>def</head>ghi",
        );
        test_injector(
            ["abc</head>d", "ef</head>ghi"],
            "abc<snippet></head>def</head>ghi",
        );
    }

    #[test]
    fn incomplete_head() {
        test_injector(["abc</he</head>def"], "abc</he<snippet></head>def");
        test_injector(["abc</he", "</head>def"], "abc</he<snippet></head>def");
        test_injector(["abc</he", "</", "head>def"], "abc</he<snippet></head>def");
    }

    #[test]
    fn casing() {
        test_injector(["abc</HeAd>def"], "abc<snippet></HeAd>def");
        test_injector(["abc</HEAD>def"], "abc<snippet></HEAD>def");
    }

    #[test]
    fn spaces() {
        test_injector(["abc </head>def"], "abc <snippet></head>def");
        test_injector(["abc< /head>def"], "abc< /head>def         ");
        test_injector(["abc</ head>def"], "abc<snippet></ head>def");
        test_injector(["abc</h ead>def"], "abc</h ead>def         ");
        test_injector(["abc</he ad>def"], "abc</he ad>def         ");
        test_injector(["abc</hea d>def"], "abc</hea d>def         ");
        test_injector(["abc</head >def"], "abc<snippet></head >def");
        test_injector(["abc</head> def"], "abc<snippet></head> def");
    }

    #[test]
    fn fuzzy() {
        struct Part {
            string: &'static str,
            has_head: bool,
        }

        impl Part {
            fn new(string: &'static str, has_head: bool) -> Self {
                Self { string, has_head }
            }
        }

        let parts: [Part; 16] = [
            Part::new("</head>", true),
            Part::new("</ head>", true),
            Part::new("</HeAd>", true),
            Part::new("</HEAD>", true),
            Part::new("</h ead>", false),
            Part::new("</he ad>", false),
            Part::new("</header>", false),
            Part::new("</h", false),
            Part::new("</", false),
            Part::new("<", false),
            Part::new(" ", false),
            Part::new("foo", false),
            Part::new("bar", false),
            Part::new("&nbsp;", false),
            Part::new("😊", false),
            Part::new("网络", false),
        ];

        let mut rng = rand::thread_rng();

        for _ in 0..1000 {
            // Build a random input from 'parts'
            let mut input = String::new();
            let mut expected = String::new();
            let mut has_head = false;

            let parts_count = Uniform::new(0, 20).sample(&mut rng);
            for part in parts.choose_multiple(&mut rng, parts_count) {
                input.push_str(part.string);

                if !has_head && part.has_head {
                    expected.push_str("<snippet>");
                    has_head = true;
                }

                expected.push_str(part.string);
            }
            if !has_head {
                expected.push_str("         ");
            }

            // Split `input` into chunks of random sizes. Because chunks might not end at a
            // character boundary, we need to use &[u8] chunks instead of &str.
            let chunks = rng
                .clone()
                .sample_iter(Uniform::new(0, input.len() + 1))
                .scan(input.as_bytes(), |input, chunk_size| {
                    if input.is_empty() {
                        None
                    } else {
                        let chunk_size = chunk_size.min(input.len());
                        let chunk = &input[..chunk_size];
                        *input = &input[chunk_size..];
                        Some(chunk)
                    }
                });

            test_injector(chunks, expected.as_str());
        }
    }

    fn test_injector<T: AsRef<[u8]> + std::fmt::Debug>(
        input_chunks: impl IntoIterator<Item = T>,
        expected: &str,
    ) {
        let snippet = b"<snippet>";
        let mut injector = Injector::new(snippet);

        let input_chunks: Vec<T> = input_chunks.into_iter().collect();

        let mut output_slices = Vec::new();
        let mut injected = false;

        for incoming_chunk in input_chunks
            .iter()
            .map(|chunk| return Some(chunk.as_ref()))
            .chain([None])
        {
            let result = match incoming_chunk {
                Some(chunk) => injector.write(chunk),
                None => injector.end(),
            };
            for slice in result.iter() {
                {
                    // Make sure the slice has a correct from_incoming_chunk flag
                    let expected_from_incoming_chunk_value = match incoming_chunk {
                        Some(chunk) => {
                            let pointer = slice.bytes.as_ptr() as usize;
                            let min = chunk.as_ptr() as usize;
                            let max = min + chunk.len();
                            min <= pointer && pointer < max
                        }
                        None => false,
                    };

                    assert_eq!(
                        slice.from_incoming_chunk,
                        expected_from_incoming_chunk_value
                    );
                }

                if !injected {
                    // Look for the snippet in the slice
                    injected = slice.bytes.windows(slice.bytes.len()).any(|w| w == snippet);
                }
                output_slices.extend_from_slice(slice.bytes);
            }
            assert_eq!(result.injected, injected);
        }

        assert_eq!(
            String::from_utf8(output_slices).unwrap(),
            expected,
            "with chunks {:?}",
            input_chunks
        );
    }
}
