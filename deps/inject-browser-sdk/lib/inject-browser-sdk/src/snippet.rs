// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache 2.0 License. This product includes software developed at
// Datadog (https://www.datadoghq.com/).
//
// Copyright 2024-Present Datadog, Inc.

use serde::Serialize;
use std::io::{self, Write};

use crate::{configuration::validate_configuration, configuration::Configuration, error::Error};

/// Generates the Datadog RUM SDK JavaScript snippet based on the provided configuration.
///
/// This function creates a JavaScript snippet that can be injected into your web application
/// to enable RUM. The snippet is generated using the provided `configuration`.
pub fn generate_snippet(configuration: &Configuration) -> Result<Vec<u8>, Error> {
    validate_configuration(configuration)?;

    let site = configuration.rum.site.as_deref().unwrap_or("datadoghq.com");
    let url = format_cdn_url(configuration.major_version, site)?;

    let mut output = Vec::new();

    output
        .write_all(
            br#"
<script>
(function(h,o,u,n,d) {
  h=h[d]=h[d]||{q:[],onReady:function(c){h.q.push(c)}}
  d=o.createElement(u);d.async=1;d.src=n
  n=o.getElementsByTagName(u)[0];n.parentNode.insertBefore(d,n)
})(window,document,'script','"#,
        )
        .unwrap();

    output.write_all(url.as_bytes()).unwrap();

    output
        .write_all(
            br#"','DD_RUM')
window.DD_RUM.onReady(function() {
  window.DD_RUM.init("#,
        )
        .unwrap();

    let mut serializer = serde_json::Serializer::with_formatter(&mut output, EscapeNonAscii);
    configuration.rum.serialize(&mut serializer).unwrap();

    output
        .write_all(
            br#");
});
</script>
"#,
        )
        .unwrap();

    Ok(output)
}

fn format_cdn_url(major_version: u32, site: &str) -> Result<String, Error> {
    if site == "ddog-gov.com" {
        return Ok(format!(
            "https://www.datadoghq-browser-agent.com/datadog-rum-v{major_version}.js"
        ));
    }

    let region = match site {
        "datadoghq.com" => "us1",
        "us3.datadoghq.com" => "us3",
        "us5.datadoghq.com" => "us5",
        "datadoghq.eu" => "eu1",
        "ap1.datadoghq.com" => "ap1",
        _ => return Err(Error::UnsupportedSite(site.to_string())),
    };

    Ok(format!(
        "https://www.datadoghq-browser-agent.com/{region}/v{major_version}/datadog-rum.js"
    ))
}

/// JSON Formatter that escapes all non-ASCII characters.
/// Based on `<https://github.com/serde-rs/json/issues/907#issuecomment-1179882369>`
struct EscapeNonAscii;

impl serde_json::ser::Formatter for EscapeNonAscii {
    fn write_string_fragment<W: ?Sized + Write>(
        &mut self,
        writer: &mut W,
        fragment: &str,
    ) -> io::Result<()> {
        for ch in fragment.chars() {
            if ch.is_ascii() {
                writer.write_all(ch.encode_utf8(&mut [0; 4]).as_bytes())?;
            } else {
                for escape in ch.encode_utf16(&mut [0; 2]) {
                    write!(writer, "\\u{:04x}", escape)?;
                }
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{configuration::PrivacyLevel, configuration::RumConfiguration};
    use pretty_assertions::assert_eq;
    use serde_json::json;
    use std::collections::BTreeMap;

    #[test]
    fn test_generate_snippet_with_minimal_configuration() {
        let expected_snippet = r#"
<script>
(function(h,o,u,n,d) {
  h=h[d]=h[d]||{q:[],onReady:function(c){h.q.push(c)}}
  d=o.createElement(u);d.async=1;d.src=n
  n=o.getElementsByTagName(u)[0];n.parentNode.insertBefore(d,n)
})(window,document,'script','https://www.datadoghq-browser-agent.com/us1/v5/datadog-rum.js','DD_RUM')
window.DD_RUM.onReady(function() {
  window.DD_RUM.init({"applicationId":"bar","clientToken":"foo"});
});
</script>
"#;

        assert_eq!(
            std::str::from_utf8(
                &generate_snippet(&Configuration {
                    major_version: 5,
                    rum: RumConfiguration {
                        client_token: Box::from("foo"),
                        application_id: Box::from("bar"),
                        ..Default::default()
                    }
                })
                .unwrap()
            )
            .unwrap(),
            expected_snippet
        )
    }

    #[test]
    fn test_generate_snippet() {
        let expected_snippet = r#"
<script>
(function(h,o,u,n,d) {
  h=h[d]=h[d]||{q:[],onReady:function(c){h.q.push(c)}}
  d=o.createElement(u);d.async=1;d.src=n
  n=o.getElementsByTagName(u)[0];n.parentNode.insertBefore(d,n)
})(window,document,'script','https://www.datadoghq-browser-agent.com/us1/v5/datadog-rum.js','DD_RUM')
window.DD_RUM.onReady(function() {
  window.DD_RUM.init({"applicationId":"bar","clientToken":"foo","site":"datadoghq.com","trackResources":true,"defaultPrivacyLevel":"mask","sessionSampleRate":42.42});
});
</script>
"#;

        assert_eq!(
            std::str::from_utf8(
                &generate_snippet(&Configuration {
                    major_version: 5,
                    rum: RumConfiguration {
                        client_token: Box::from("foo"),
                        application_id: Box::from("bar"),
                        site: Some(Box::from("datadoghq.com")),
                        default_privacy_level: Some(PrivacyLevel::Mask),
                        track_resources: Some(true),
                        session_sample_rate: Some(42.42),
                        ..Default::default()
                    }
                })
                .unwrap()
            )
            .unwrap(),
            expected_snippet
        )
    }

    #[test]
    fn unicode_values() {
        // It is important that all non-ascii values are escaped, because we don't know the page
        // encoding.
        let expected_snippet = r#"
<script>
(function(h,o,u,n,d) {
  h=h[d]=h[d]||{q:[],onReady:function(c){h.q.push(c)}}
  d=o.createElement(u);d.async=1;d.src=n
  n=o.getElementsByTagName(u)[0];n.parentNode.insertBefore(d,n)
})(window,document,'script','https://www.datadoghq-browser-agent.com/us1/v5/datadog-rum.js','DD_RUM')
window.DD_RUM.onReady(function() {
  window.DD_RUM.init({"applicationId":"\u263a \u20ac \u00e9","clientToken":"foo","site":"datadoghq.com"});
});
</script>
"#;

        assert_eq!(
            std::str::from_utf8(
                &generate_snippet(&Configuration {
                    major_version: 5,
                    rum: RumConfiguration {
                        client_token: Box::from("foo"),
                        application_id: Box::from("☺ € é"),
                        site: Some(Box::from("datadoghq.com")),
                        ..Default::default()
                    }
                })
                .unwrap()
            )
            .unwrap(),
            expected_snippet
        )
    }

    #[test]
    fn test_forward_compatibility() {
        let expected_snippet = r#"
<script>
(function(h,o,u,n,d) {
  h=h[d]=h[d]||{q:[],onReady:function(c){h.q.push(c)}}
  d=o.createElement(u);d.async=1;d.src=n
  n=o.getElementsByTagName(u)[0];n.parentNode.insertBefore(d,n)
})(window,document,'script','https://www.datadoghq-browser-agent.com/us1/v5/datadog-rum.js','DD_RUM')
window.DD_RUM.onReady(function() {
  window.DD_RUM.init({"applicationId":"foo","clientToken":"bar","site":"datadoghq.com","newopt":"value","newopt2":true});
});
</script>
"#;

        assert_eq!(
            std::str::from_utf8(
                &generate_snippet(&Configuration {
                    major_version: 5,
                    rum: RumConfiguration {
                        application_id: Box::from("foo"),
                        client_token: Box::from("bar"),
                        site: Some(Box::from("datadoghq.com")),
                        other: BTreeMap::from([
                            (Box::from("newopt"), json!("value")),
                            (Box::from("newopt2"), json!(true)),
                        ]),
                        ..Default::default()
                    }
                })
                .unwrap()
            )
            .unwrap(),
            expected_snippet
        )
    }

    #[test]
    fn test_format_cdn_url() {
        assert_eq!(
            format_cdn_url(5, "datadoghq.com").unwrap(),
            "https://www.datadoghq-browser-agent.com/us1/v5/datadog-rum.js"
        );
        assert_eq!(
            format_cdn_url(5, "foo.com").unwrap_err(),
            Error::UnsupportedSite(String::from("foo.com"))
        );
        assert_eq!(
            format_cdn_url(5, "ddog-gov.com").unwrap(),
            "https://www.datadoghq-browser-agent.com/datadog-rum-v5.js"
        );
    }
}
