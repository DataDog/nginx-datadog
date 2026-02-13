// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache 2.0 License. This product includes software developed at
// Datadog (https://www.datadoghq.com/).
//
// Copyright 2024-Present Datadog, Inc.

use inject_browser_sdk::{generate_snippet, Configuration};

use std::{env, fs, process};

fn main() {
    let args: Vec<String> = env::args().collect();

    if args.len() < 2 {
        eprintln!("Usage: {} <json_file>", args[0]);
        process::exit(1);
    }

    let json_file = &args[1];

    let file_content = fs::read_to_string(json_file).unwrap_or_else(|err| {
        eprintln!("Error reading file {}: {}", json_file, err);
        process::exit(1);
    });

    let conf: Configuration = serde_json::from_str(&file_content).unwrap_or_else(|err| {
        eprintln!("Error parsing JSON: {}", err);
        process::exit(1);
    });

    let snippet = generate_snippet(&conf).unwrap_or_else(|err| {
        eprintln!("Error parsing JSON: {}", err);
        process::exit(1);
    });

    if let Ok(str) = String::from_utf8(snippet) {
        println!("{}", str);
    } else {
        eprintln!("Error constructing the string");
        process::exit(1);
    }

    process::exit(0);
}
