-- datadog_rum.lua
--[[
  Datadog RUM injection
  Injects the Datadog RUM script into 'text/html' responses.
  RUM documentation available at https://docs.datadoghq.com/real_user_monitoring/browser/#configuration
    Configuration parameters:
    - enabled: boolean, whether to enable the injection, defaults to true
    - stream_response: boolean, whether to stream or to buffer the response before injecting the script, default: true
    - major_version: number, the major version of the Datadog RUM SDK to use, defaults to 5
    - cdn_region: string, the region of the CDN to use, defaults to 'us1'
    - inject_before: string, the string before which the script will be injected, defaults to '</head>'
    - inject_times: number, the number of times to inject the script, defaults to 1
    - rum: table, the configuration to pass to the Datadog RUM init function, defaults to {}
    - csp_additions: string, value to append to the 'content-security-policy' header, defaults to no value
--]]
local PENDING_HEADER_NAME = 'x-datadog-rum-injection-pending'
local INJECTED_HEADER_NAME = 'x-datadog-rum-injected'
local INJECTED_MIMETYPE = 'text/html'

local CONFIG_KEY = 'datadog_rum'
local CSP_ADDITIONS = ''
local DEFAULT_SDK_VERSION = '5'
local DEFAULT_CDN_REGION = 'us1'
local DEFAULT_INIT_CONFIG = '{}'

local DEFAULT_INJECT_BEFORE = '</head>'
local DEFAULT_INJECT_TIMES = 1
local DEFAULT_STREAMING = true

---Gets the value if it matches the expected type, otherwise returns the default value
---@generic T
---@param value? unknown
---@param expected_type `T`
---@param default T
---@return T
local function value_type_or_default(value, expected_type, default)
  if value == nil or type(value) ~= expected_type then
    return default
  end
  return value
end

---@param obj unknown
---@return string
local function serialize_obj(obj)
  if type(obj) == 'table' then
    local s = '{'
    for k, v in pairs(obj) do
      s = s .. '"' .. k .. '":' .. serialize_obj(v) .. ','
    end
    return s .. '}'
  elseif type(obj) == 'string' then
    return '"' .. obj .. '"'
  elseif type(obj) == 'number' or type(obj) == 'boolean' then
    return tostring(obj)
  else
    return ''
  end
end

---@alias MetadataValue string | number | boolean | table<string, MetadataValue>
---@alias Metadata table<string, MetadataValue>
---@param metadata Metadata
---@return string major_version
---@return string cdn_region
---@return string init_config
local function parse_config(metadata)
  local major_version = DEFAULT_SDK_VERSION
  local cdn_region = DEFAULT_CDN_REGION
  local init_config = DEFAULT_INIT_CONFIG

  if not metadata then goto result end

  for key, value in pairs(metadata) do
    if key == 'major_version' and type(value) == 'number' then
      major_version = tostring(value)
    elseif key == 'cdn_region' and type(value) == 'string' then
      cdn_region = value
    elseif key == 'rum' and type(value) == 'table' then
      init_config = serialize_obj(value)
    end
  end
  ::result::
  return major_version, cdn_region, init_config
end

---@param metadata Metadata
---@return string
local function build_tag(metadata)
  local major_version, cdn_region, init_config = parse_config(metadata)
  local script =
    [[
    <script type="text/javascript">
      (function(h,o,u,n,d) {
        h=h[d]=h[d]||{q:[],onReady:function(c){h.q.push(c)}}
        d=o.createElement(u);d.async=1;d.src=n
        n=o.getElementsByTagName(u)[0];n.parentNode.insertBefore(d,n)
      })(window,document,'script','https://www.datadoghq-browser-agent.com/]] .. cdn_region .. [[/v]] ..
    major_version .. [[/datadog-rum.js','DD_RUM')
      window.DD_RUM.onReady(function() {
        window.DD_RUM.init(]] .. init_config .. [[);
      })
    </script>
    ]]
  return script
end

---Handles setting the "injected" header, if not set, and returns whether the header was set
---@param response_handle any
---@return boolean
local function handle_injection_header(response_handle)
  ---@type string|nil
  local injected_header = response_handle:headers():get(INJECTED_HEADER_NAME)
  if injected_header ~= nil then
    return false
  end
  response_handle:headers():add(INJECTED_HEADER_NAME, '1')
  return true
end

---Sets the 'content-security-policy' header with the given additions
---@param response_handle any
---@param csp_additions string
local function set_csp_additions(response_handle, csp_additions)
  local csp = response_handle:headers():get('content-security-policy')

  if csp ~= '' then
    csp_additions = '; ' .. csp_additions
  end
  response_handle:headers():replace('content-security-policy', (csp or '') .. csp_additions)
end

---Handles setting the 'content-security-policy' header
---@param response_handle any
---@param metadata Metadata
local function handle_csp_header(response_handle, metadata)
  local csp_additions = value_type_or_default(metadata['csp_additions'], 'string', CSP_ADDITIONS)
  if csp_additions ~= nil and csp_additions ~= '' then
    set_csp_additions(response_handle, csp_additions)
  end
end

---Looks up a partial match of a string in another string
---@param raw_string string
---@param needle string
---@return integer?
local function lookup_partial(raw_string, needle)
  local needle_length = #(needle)
  local haystack = raw_string:reverse():sub(1, needle_length):reverse()
  local haystack_length = #(haystack)

  local start
  while needle_length > 0 do
    local searching = needle:sub(1, needle_length)
    if haystack:sub(haystack_length - needle_length + 1) == searching then
      start = haystack_length - needle_length + 1
    end
    if start ~= nil then
      break
    end
    needle_length = needle_length - 1
  end
  if start ~= nil then
    return #(raw_string) - (haystack_length - start)
  else
    return nil
  end
end

---Attempts to match a string with a needle even if partially found, and returns the string before the match and the
---rest of the string. If no match is found, returns the entire string and nil.
---@param raw_string string
---@param needle string
---@return string
---@return string
local function speculative_match(raw_string, needle)
  local chunk, rest
  local partial_match = lookup_partial(raw_string, needle)
  if partial_match then
    chunk = raw_string:sub(1, partial_match - 1)
    rest = raw_string:sub(partial_match)
  else
    chunk = raw_string
  end
  return chunk, rest
end

---Replaces all occurrences of a string in another string, and returns the modified string and the count of replacements
---plus the remainder since the last match until the end of the string.
---@param haystack string
---@param needle string
---@param replacement string
---@param times integer
---@return string
---@return integer
---@return string?
local function chunked_replace(haystack, needle, replacement, times)
  local buffer = ''
  local found_from, found_to = haystack:find(needle, 1, true)
  local found_times = 0
  while found_from ~= nil and found_times < times do
    found_times = found_times + 1
    if (found_times == 1 and found_from > 1) then
      buffer = haystack:sub(1, found_from - 1)
    end
    buffer = buffer .. replacement .. needle
    haystack = haystack:sub(found_to + 1)
    found_from, found_to = haystack:find(needle, 1, true)
  end
  local chunk, rest
  if found_times < times and #(haystack) > 0 then
    chunk, rest = speculative_match(haystack, needle)
    buffer = buffer .. chunk
  elseif #(haystack) > 0 then
    buffer = buffer .. haystack
  end
  return buffer, found_times, rest
end

---Replaces all occurrences of a string in another string, and returns the modified string and the count of replacements
---@param haystack string
---@param needle string
---@param replacement string
---@param times integer
---@return string
---@return integer
local function buffered_replace(haystack, needle, replacement, times)
  local modified, count = haystack:gsub(needle, replacement, times)
  if count == 0 then
    return haystack, 0
  end

  return modified, count
end

---Performs the injection of the RUM tag into the response, and returns the replacement count and the rest of the string
---@param response_handle any
---@param body any
---@param body_string string
---@param inject_before string
---@param inject_times integer
---@param metadata Metadata
---@param buffered boolean
---@return integer
---@return string|nil
local function perform_injection(response_handle, body, body_string, inject_before, inject_times, metadata, buffered)
  local script = build_tag(metadata)

  local count = 0
  local rest = ''
  if buffered then
    local modified
    modified, count = buffered_replace(body_string, inject_before, script .. inject_before, inject_times)
    if count > 0 then
      response_handle:headers():replace('content-length', #(modified))
      body:setBytes(modified)
    end
  else -- chunked
    local modified, c_count, c_rest = chunked_replace(rest .. body_string, inject_before, script, inject_times)
    body:setBytes(modified)
    count = count + c_count
    if c_rest ~= nil then
      rest = c_rest
    end
  end
  return count, rest
end

---Injects the RUM tag into a chunked response
---@param response_handle any
---@param metadata Metadata
local function inject_chunked_response(response_handle, metadata)
  response_handle:headers():remove('content-length')
  -- Preemptively set the CSP value
  handle_csp_header(response_handle, metadata)

  local inject_before = value_type_or_default(metadata['inject_before'], 'string', DEFAULT_INJECT_BEFORE)
  local inject_times = value_type_or_default(metadata['inject_times'], 'number', DEFAULT_INJECT_TIMES)

  local injection_count = 0

  local rest = ''
  for chunk in response_handle:bodyChunks() do
    local chunk_length = chunk:length()
    if chunk_length == 0 then goto continue end

    local chunk_string = chunk:getBytes(0, chunk_length)

    if rest and rest ~= '' then
      chunk_string = rest .. chunk_string
    end

    local times_left = inject_times - injection_count

    local c, r = perform_injection(response_handle, chunk, chunk_string, inject_before, times_left, metadata, false)
    injection_count = injection_count + c
    if r ~= nil then
      rest = r
    end
    if injection_count >= inject_times then
      break
    end
    ::continue::
  end
end

---Injects the RUM tag into a buffered response
---@param response_handle any
---@param metadata Metadata
local function inject_buffered_response(response_handle, metadata)
  local body = response_handle:body()
  if body == nil then
    return
  end

  local body_string = body:getBytes(0, body:length())
  local inject_before = value_type_or_default(metadata['inject_before'], 'string', DEFAULT_INJECT_BEFORE)
  local inject_times = value_type_or_default(metadata['inject_times'], 'number', DEFAULT_INJECT_TIMES)
  local injection_count =
    perform_injection(response_handle, body, body_string, inject_before, inject_times, metadata, true)

  if injection_count > 0 then
    handle_csp_header(response_handle, metadata)
  end
end

---Injects the RUM tag into the response
---@param response_handle any
local function inject_rum_tag(response_handle)
  local metadata = response_handle:metadata():get(CONFIG_KEY)
  if metadata == nil or metadata['enabled'] == false then
    return
  end

  local contentType = response_handle:headers():get('content-type')
  if contentType == nil or contentType:sub(1, #(INJECTED_MIMETYPE)) ~= INJECTED_MIMETYPE then
    return
  end

  if not handle_injection_header(response_handle) then
    return
  end

  local stream_response = value_type_or_default(metadata['stream_response'], 'boolean', DEFAULT_STREAMING)
  if stream_response then
    inject_chunked_response(response_handle, metadata)
  else
    inject_buffered_response(response_handle, metadata)
  end
end

---Handles imprinting the request with the pending header
---@param request_handle any
local function handle_request(request_handle)
  if request_handle:headers():get(PENDING_HEADER_NAME) ~= nil then
    return
  end
  request_handle:headers():add(PENDING_HEADER_NAME, '1')
end

RUMInternals = {
  serialize_obj = serialize_obj,
  parse_config = parse_config,
  value_type_or_default = value_type_or_default,
  build_tag = build_tag,
  handle_injection_header = handle_injection_header,
  handle_csp_header = handle_csp_header,
  perform_injection = perform_injection,
  inject_chunked_response = inject_chunked_response,
  inject_buffered_response = inject_buffered_response,
  buffered_replace = buffered_replace,
  speculative_match = speculative_match,
  lookup_partial = lookup_partial,
  set_csp_additions = set_csp_additions,
  PENDING_HEADER_NAME = PENDING_HEADER_NAME,
  INJECTED_HEADER_NAME = INJECTED_HEADER_NAME,
  INJECTED_MIMETYPE = INJECTED_MIMETYPE,
  CONFIG_KEY = CONFIG_KEY,
  CSP_ADDITIONS = CSP_ADDITIONS,
  DEFAULT_SDK_VERSION = DEFAULT_SDK_VERSION,
  DEFAULT_CDN_REGION = DEFAULT_CDN_REGION,
  DEFAULT_INIT_CONFIG = DEFAULT_INIT_CONFIG,
  DEFAULT_INJECT_BEFORE = DEFAULT_INJECT_BEFORE,
  DEFAULT_INJECT_TIMES = DEFAULT_INJECT_TIMES,
  DEFAULT_STREAMING = DEFAULT_STREAMING
}

DatadogRUM = {
  inject_response = inject_rum_tag,
  imprint_request = handle_request
}
-- export locals for test
if _TEST then
  DatadogRUM.__internals = RUMInternals
end

function envoy_on_request(request_handle)
  DatadogRUM.imprint_request(request_handle)
end

function envoy_on_response(response_handle)
  DatadogRUM.inject_response(response_handle)
end

return DatadogRUM
-- Returning this object makes it possible to load the module as a library in other Lua scripts.
-- For example, to customize the envoy_on_request and envoy_on_response callbacks in the envoy.yaml config:
--[[
  (... in http_connection_manager ...)
    http_filters:
    - name: datadog_rum
      typed_config:
        '@type': type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
        default_source_code:
          inline_string: |
            local DatadogRUM = require('usr.local.lib.datadog_rum')

            function envoy_on_request(request_handle)
              DatadogRUM.imprint_request(request_handle)
            end

            function envoy_on_response(response_handle)
              DatadogRUM.inject_response(response_handle)
            end
]]
