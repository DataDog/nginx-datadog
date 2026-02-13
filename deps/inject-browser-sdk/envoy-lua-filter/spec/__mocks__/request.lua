local Dict = require('spec.__mocks__.dict')

---@class RequestHandle
---@field _headers Dict
RequestHandle = {}
RequestHandle.__index = RequestHandle

---@param headers? table<string, DictValue>
---@return RequestHandle
function RequestHandle.new(headers)
  local self = setmetatable({}, RequestHandle)
  self._headers = Dict.new(headers)
  return self
end

---@return Dict
function RequestHandle:headers()
  return self._headers
end

return RequestHandle
