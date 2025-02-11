stds.envoy_filter = {
  'envoy_on_request',
  'envoy_on_response',
}
std = 'luajit+busted+envoy_filter'
read_globals = {
  '_TEST'
}
allow_defined_top = true
files['**/src/datadog_rum.lua'] = {
  ignore = {"131/.*envoy_on_.*"}
}
