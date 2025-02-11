local ResponseBodyBuffered = require('spec.__mocks__.response_body_buffered')

---@class ResponseBodyChunked
---@field _body ResponseBodyBuffered[]
ResponseBodyChunked = {}
ResponseBodyChunked.__index = ResponseBodyChunked

---@param body? ResponseBodyBuffered[]
---@return ResponseBodyChunked
function ResponseBodyChunked.new(body)
  local self = setmetatable({}, ResponseBodyChunked)
  if body ~= nil and type(body) == 'table' then
    self._body = {}
    for _, bodyChunk in ipairs(body) do
      table.insert(self._body, bodyChunk)
    end
  else
    self._body = { ResponseBodyBuffered.new() }
  end
  return self
end

---@alias bodyChunks fun():ResponseBodyBuffered
---@return bodyChunks
function ResponseBodyChunked:bodyChunks()
  return coroutine.wrap(function()
    for i = 1, #self._body do
      coroutine.yield(self._body[i])
    end
  end)
end

return ResponseBodyChunked
