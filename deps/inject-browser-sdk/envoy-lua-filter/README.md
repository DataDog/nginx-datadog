# RUM Browser SDK injection

## Envoy Lua Filter

### Developing

#### Development Requirements

On **macos**
- [Lua](https://formulae.brew.sh/formula/lua) >= 5.4
- [Luarocks](https://formulae.brew.sh/formula/luarocks) >= 3.10.0
- (optional) [fswatch](https://formulae.brew.sh/formula/fswatch)

#### Development Setup

- Clone the repo
- Run `cd inject-browser-sdk/envoy-lua-filter`
- Run `luarocks install --deps-only ./datadog-rum-1.0.0-1.rockspec`

Additionally, if you want to watch for changes and check the formatting:
- Install [fswatch](https://formulae.brew.sh/formula/fswatch)
- Run `./watch_format.sh`
