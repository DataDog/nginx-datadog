// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache 2.0 License. This product includes software developed at
// Datadog (https://www.datadoghq.com/).
//
// Copyright 2024-Present Datadog, Inc.

/// The location of an injection point, as computed by the InjectionPointLocator
#[cfg_attr(test, derive(Debug))]
pub enum Location {
    /// Nothing was found. If a potential injection point was previously notified, it should be
    /// dismissed.
    None,

    /// A potential injection point was found in the current chunk. More data is needed to confirm
    /// that it is a valid injection point. If a potential injection point was previously notified,
    /// it should be dismissed.
    PotentialFromIndex(usize),

    /// The potential injection point that started within a previous chunk is still unconfirmed.
    /// More data is needed to confirm that it is a valid injection point.
    PotentialFromPreviousChunk,

    /// An injection point was found in the current chunk. If a potential injection point was
    /// previously notified, it should be dismissed.
    MatchFromIndex(usize),

    /// The potential injection point that started within a previous chunk is confirmed.
    MatchFromPreviousChunk,
}

/// Stateful object used to locate the "injection point", in other words where we should inject the
/// snippet in an HTML document.
///
/// The current implementation is focusing on locating the `</head>` tag, so we can inject the
/// snippet right before it. It is the equivalent of the regex `/<\/\s*head\s*>/i`: case
/// insensitive and allows spaces before and after 'head'.
pub struct InjectionPointLocator {
    /// Goes from 0 to 6, where each value correspond to an expected character of `</head>`.
    /// 0 -> we are looking for the character '<'
    /// 1 -> we are looking for the character '/'
    /// 2 -> we are looking for the character 'h'
    /// etc.
    index: u8,
}

impl InjectionPointLocator {
    pub fn new() -> Self {
        Self { index: 0 }
    }

    pub fn scan(&mut self, chunk: &[u8]) -> Location {
        let mut location = if self.index > 0 {
            Location::PotentialFromPreviousChunk
        } else {
            Location::None
        };

        for (index, mut byte) in chunk.iter().copied().enumerate() {
            if self.index > 1 {
                byte = byte.to_ascii_lowercase();
            }

            match (self.index, byte) {
                // Consider any '<' character as a potential injection point.
                (_, b'<') => {
                    self.index = 1;
                    location = Location::PotentialFromIndex(index);
                }

                // If we find the expected character, increment `index` to look for the next one.
                (1, b'/') | (2, b'h') | (3, b'e') | (4, b'a') | (5, b'd') => {
                    self.index += 1;
                }

                // If we find the last character '>', reset the state and return the matched
                // location.
                (6, b'>') => {
                    self.index = 0;
                    location = match location {
                        Location::PotentialFromPreviousChunk => Location::MatchFromPreviousChunk,
                        Location::PotentialFromIndex(index) => Location::MatchFromIndex(index),
                        _ => unreachable!(),
                    };
                    break;
                }

                // Ignore whitespaces before characters 'h' and '>'.
                (2 | 6, byte) if byte.is_ascii_whitespace() => {}

                // Any other character is unexpected, reset the state.
                _ => {
                    self.index = 0;
                    location = Location::None;
                }
            }
        }

        location
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use pretty_assertions::assert_matches;

    #[test]
    fn basic() {
        let mut locator = InjectionPointLocator::new();
        assert_matches!(locator.scan(b"abc</head>def"), Location::MatchFromIndex(3));
    }

    #[test]
    fn streaming() {
        let mut locator = InjectionPointLocator::new();
        assert_matches!(locator.scan(b""), Location::None);
        assert_matches!(locator.scan(b"a"), Location::None);
        assert_matches!(locator.scan(b" "), Location::None);
        assert_matches!(locator.scan(b" <"), Location::PotentialFromIndex(1));
        assert_matches!(locator.scan(b""), Location::PotentialFromPreviousChunk);
        assert_matches!(locator.scan(b"/"), Location::PotentialFromPreviousChunk);
        assert_matches!(locator.scan(b"head>"), Location::MatchFromPreviousChunk);
        assert_matches!(locator.scan(b""), Location::None);
        assert_matches!(locator.scan(b"a"), Location::None);
    }

    #[test]
    fn casing() {
        let mut locator = InjectionPointLocator::new();
        assert_matches!(locator.scan(b"abc</HEAD>def"), Location::MatchFromIndex(3));
    }

    #[test]
    fn space_tolerant() {
        let mut locator = InjectionPointLocator::new();
        assert_matches!(
            locator.scan(b"abc</\n\r head \n\t>def"),
            Location::MatchFromIndex(3)
        );
    }
}
