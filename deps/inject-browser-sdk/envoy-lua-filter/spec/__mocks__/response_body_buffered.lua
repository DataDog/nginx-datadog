---@class ResponseBodyBuffered
---@field _body string
ResponseBodyBuffered = {}
ResponseBodyBuffered.__index = ResponseBodyBuffered

---@param body? string
---@return ResponseBodyBuffered
function ResponseBodyBuffered.new(body)
  local self = setmetatable({}, ResponseBodyBuffered)
  if body then
    self._body = body
  else
    self._body = ''
  end
  return self
end

---@param start integer
---@param finish integer
function ResponseBodyBuffered:getBytes(start, finish)
  return string.sub(self._body, start, finish)
end

---@param body string
function ResponseBodyBuffered:setBytes(body)
  self._body = body
end

function ResponseBodyBuffered:length()
  return string.len(self._body)
end

return ResponseBodyBuffered
