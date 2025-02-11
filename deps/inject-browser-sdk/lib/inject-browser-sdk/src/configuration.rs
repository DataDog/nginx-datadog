// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache 2.0 License. This product includes software developed at
// Datadog (https://www.datadoghq.com/).
//
// Copyright 2024-Present Datadog, Inc.

use crate::error::Error;
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::collections::BTreeMap as Map;

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
#[cfg_attr(test, derive(Debug))]
#[allow(missing_docs)]
pub struct Configuration {
    pub major_version: u32,
    pub rum: RumConfiguration,
}

#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
#[cfg_attr(test, derive(Debug, Default, PartialEq))]
/// Configuration settings for Real User Monitoring (RUM).
///
/// This struct allows you to customize various aspect of RUM.
/// It also supports additional, unknown fields for forward
/// compatibility with future updates
pub struct RumConfiguration {
    /// RUM Applicatin ID
    pub application_id: Box<str>,
    /// The client token provided by Datadog to authenticate requests
    pub client_token: Box<str>,
    /// The Datadog site to which data will be sent (e.g., `datadoghq.com`)
    #[serde(skip_serializing_if = "Option::is_none")]
    pub site: Option<Box<str>>,
    /// The name of the service being monitored.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub service: Option<Box<str>>,
    /// The environment of the service (e.g., `prod`, `staging` or `dev)
    #[serde(skip_serializing_if = "Option::is_none")]
    pub env: Option<Box<str>>,
    /// The version of the service (e.g., `0.1.0`, `a8dj92`, `2024-30`)
    #[serde(skip_serializing_if = "Option::is_none")]
    pub version: Option<Box<str>>,
    /// Enables or disables the automatic collection of users actions (e.g., clicks)
    #[serde(skip_serializing_if = "Option::is_none")]
    pub track_user_interactions: Option<bool>,
    /// Enables or disables the collection of resource events (e.g., loading of images or scripts)
    #[serde(skip_serializing_if = "Option::is_none")]
    pub track_resources: Option<bool>,
    /// Enables or disables the collection of long task events
    #[serde(skip_serializing_if = "Option::is_none")]
    pub track_long_task: Option<bool>,
    /// Set the privacy level for data collection
    #[serde(skip_serializing_if = "Option::is_none")]
    pub default_privacy_level: Option<PrivacyLevel>,
    /// The percentage of user sessions to be tracked.
    ///
    /// The value should be between 0.0 and 100.0
    #[serde(skip_serializing_if = "Option::is_none")]
    pub session_sample_rate: Option<f32>,
    /// The percentage of tracked sessions that will include Session Replay data
    ///
    /// The value should be between 0.0 and 100.0
    #[serde(skip_serializing_if = "Option::is_none")]
    pub session_replay_sample_rate: Option<f32>,
    /// Stores any additional fields that are not recognized by this struct.
    ///
    /// This enables the configuration to be forward-compatible with future updates.
    #[serde(flatten)]
    pub other: Map<Box<str>, Value>,
}

#[derive(Serialize, Deserialize)]
#[serde(rename_all = "kebab-case")]
#[cfg_attr(test, derive(Debug, PartialEq))]
#[allow(missing_docs)]
pub enum PrivacyLevel {
    Allow,
    Mask,
    MaskUserInput,
}

const SITE_PATTERNS: [&str; 4] = ["datadog", "ddog", "datad0g", "dd0g"];
const MAJOR_VERSIONS: [u32; 2] = [5, 6];

pub fn validate_configuration(conf: &Configuration) -> Result<(), Error> {
    if !MAJOR_VERSIONS.contains(&conf.major_version) {
        return Err(Error::UnsupportedMajorVersion(conf.major_version));
    }

    if conf.rum.application_id.is_empty() {
        return Err(Error::EmptyMandatoryConf(String::from("application_id")));
    }

    if conf.rum.client_token.is_empty() {
        return Err(Error::EmptyMandatoryConf(String::from("client_token")));
    }

    if let Some(site) = &conf.rum.site {
        if !SITE_PATTERNS
            .iter()
            .any(|&pattern| site.as_ref().contains(pattern))
        {
            return Err(Error::UnsupportedSite(site.to_string()));
        }
    }

    let validate_rate = |key: &str, maybe_rate: Option<f32>| {
        if let Some(rate) = maybe_rate {
            if !(0.0..=100.0).contains(&rate) {
                return Err(Error::OutOfRangeRate(String::from(key), rate));
            }
        }
        Ok(())
    };

    validate_rate("session sample rate", conf.rum.session_sample_rate)?;
    validate_rate(
        "session replay sample rate",
        conf.rum.session_replay_sample_rate,
    )?;

    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use pretty_assertions::assert_eq;

    #[test]
    fn from_json() {
        let configuration: Configuration = serde_json::from_slice(
            br#"
{
  "majorVersion": 5,
  "rum": {
    "clientToken": "foo",
    "applicationId": "bar",
    "site": "datadoghq.com",
    "newConfig": "a",
    "newConfig2": 42
  }
}
"#,
        )
        .unwrap();

        let expected_new_fields = Map::from([
            (Box::from("newConfig"), Value::from("a")),
            (Box::from("newConfig2"), Value::from(42)),
        ]);

        assert_eq!(configuration.major_version, 5);
        assert_eq!(configuration.rum.client_token.as_ref(), "foo");
        assert_eq!(configuration.rum.application_id.as_ref(), "bar");
        assert_eq!(configuration.rum.other, expected_new_fields);
    }

    #[test]
    fn test_invalid_configurations() {
        struct TestCase {
            input: Configuration,
            expected: Error,
        }

        let test_cases = vec![
            TestCase {
                input: Configuration {
                    major_version: 6,
                    rum: RumConfiguration {
                        application_id: Box::from(""),
                        client_token: Box::from("foo"),
                        ..Default::default()
                    },
                },
                expected: Error::EmptyMandatoryConf(String::from("application_id")),
            },
            TestCase {
                input: Configuration {
                    major_version: 5,
                    rum: RumConfiguration {
                        application_id: Box::from("bar"),
                        client_token: Box::from(""),
                        ..Default::default()
                    },
                },
                expected: Error::EmptyMandatoryConf(String::from("client_token")),
            },
            TestCase {
                input: Configuration {
                    major_version: 5,
                    rum: RumConfiguration {
                        client_token: Box::from("foo"),
                        application_id: Box::from("bar"),
                        session_sample_rate: Some(105.),
                        ..Default::default()
                    },
                },
                expected: Error::OutOfRangeRate(String::from("session sample rate"), 105.),
            },
            TestCase {
                input: Configuration {
                    major_version: 5,
                    rum: RumConfiguration {
                        client_token: Box::from("foo"),
                        application_id: Box::from("bar"),
                        session_replay_sample_rate: Some(2469.),
                        ..Default::default()
                    },
                },
                expected: Error::OutOfRangeRate(String::from("session replay sample rate"), 2469.),
            },
            TestCase {
                input: Configuration {
                    major_version: 4,
                    rum: RumConfiguration {
                        ..Default::default()
                    },
                },
                expected: Error::UnsupportedMajorVersion(4),
            },
            TestCase {
                input: Configuration {
                    major_version: 5,
                    rum: RumConfiguration {
                        client_token: Box::from("foo"),
                        application_id: Box::from("bar"),
                        site: Some(Box::from("example.com")),
                        ..Default::default()
                    },
                },
                expected: Error::UnsupportedSite(String::from("example.com")),
            },
        ];

        for test_case in test_cases {
            assert_eq!(
                validate_configuration(&test_case.input).unwrap_err().code(),
                test_case.expected.code()
            );
        }
    }
}
