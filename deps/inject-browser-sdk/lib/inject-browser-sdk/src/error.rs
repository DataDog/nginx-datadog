// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache 2.0 License. This product includes software developed at
// Datadog (https://www.datadoghq.com/).
//
// Copyright 2024-Present Datadog, Inc.

#[derive(Debug, PartialEq)]
pub enum Error {
    Json(String),
    UnsupportedMajorVersion(u32),
    UnsupportedSite(String),
    OutOfRangeRate(String, f32),
    EmptyMandatoryConf(String),
}

impl Error {
    pub fn code(&self) -> u8 {
        match self {
            Error::Json(_) => 1,
            Error::UnsupportedMajorVersion(_) => 2,
            Error::UnsupportedSite(_) => 3,
            Error::OutOfRangeRate(_, _) => 4,
            Error::EmptyMandatoryConf(_) => 5,
        }
    }
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Error::Json(cause) => write!(f, "JSON error: {cause}"),
            Error::UnsupportedMajorVersion(version) => {
                write!(f, "Validation error: The major version '{version}' is not supported. Supported RUM SDK versions: [5, 6]")
            }
            Error::UnsupportedSite(site) => {
                write!(f, "Validation error: The site '{site}' is not a valid Datadog site. Examples of valid Datadog sites: 'datadoghq.com', 'datadoghq.eu', 'ddog-gov.com'.")
            }
            Error::OutOfRangeRate(key, value) => {
                write!(f, "Validation error: The provided {key} is invalid. It must be between 0.0 and 100.0. However, the received value was '{value}'.")
            }
            Error::EmptyMandatoryConf(key) => {
                write!(f, "Validation error: Mandatory field '{key}' is empty.")
            }
        }
    }
}
