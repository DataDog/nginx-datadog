# inject-browser-sdk

Shared library that injects Browser SDK snippets in HTML documents.

[Reference](https://datadoghq.dev/inject-browser-sdk)

## Requirements

You will need to have a Rust toolchain installed.

## Usage

Build the library:

```sh
cd lib
cargo build
# or
cargo build --release
```

## Approximative library sizes

Compiled on Windows, with `--release`:

| json library                                                                         | size | branch                                                                                          |
| ------------------------------------------------------------------------------------ | ---- | ----------------------------------------------------------------------------------------------- |
| serde, [serde_json](https://github.com/serde-rs/json)                                | 164K | [main](https://github.com/DataDog/inject-browser-sdk/tree/main)                                 |
| serde, [serde_json_core](https://github.com/rust-embedded-community/serde-json-core) | 156K | [test-serde-json-core](https://github.com/DataDog/inject-browser-sdk/tree/test-serde-json-core) |
| [miniserde](https://github.com/dtolnay/miniserde)                                    | 148K | [test-miniserde](https://github.com/DataDog/inject-browser-sdk/tree/test-miniserde)             |
| (no json)                                                                            | 140K | [test-no-json](https://github.com/DataDog/inject-browser-sdk/tree/test-no-json)                 |
