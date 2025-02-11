local Dict = require('spec.__mocks__.dict')
local ResponseBodyBuffered = require('spec.__mocks__.response_body_buffered')
local ResponseBodyChunked = require('spec.__mocks__.response_body_chunked')

---@class ResponseHandle
---@field _headers Dict
---@field _metadata Dict
---@field _body ResponseBodyBuffered | ResponseBodyChunked
ResponseHandle = {}
ResponseHandle.__index = ResponseHandle

---@param headers? table<string, DictValue>
---@param metadata? table<string, DictValue>
---@param body? string | ResponseBodyBuffered[]
---@param buffered? boolean Defaults to false
---@return ResponseHandle
function ResponseHandle.new(headers, metadata, body, buffered)
  local self = setmetatable({}, ResponseHandle)
  self._headers = Dict.new(headers)
  self._metadata = Dict.new(metadata)
  if buffered and type(body) == 'string' then
    self._body = ResponseBodyBuffered.new(body or '')
  elseif type(body) == 'table' then
    ---@cast body ResponseBodyBuffered[]|nil
    self._body = ResponseBodyChunked.new(body)
  else
    self._body = ResponseBodyChunked.new()
  end
  return self
end

function ResponseHandle:headers()
  return self._headers
end

function ResponseHandle:metadata()
  return self._metadata
end

---@return ResponseBodyBuffered
function ResponseHandle:body()
  local body = self._body
  ---@cast body ResponseBodyBuffered
  return body
end

---@return bodyChunks
function ResponseHandle:bodyChunks()
  local body = self._body
  ---@cast body ResponseBodyChunked
  return body:bodyChunks()
end

return ResponseHandle
