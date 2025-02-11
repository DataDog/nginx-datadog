---@alias DictPrimitive integer|string|boolean|nil
---@alias DictValue DictPrimitive | DictPrimitive[]
---@class Dict
---@field _dict table<string, DictValue>
Dict = {}
Dict.__index = Dict

---@param initial? table<string, DictValue>
---@return Dict
function Dict.new(initial)
  local self = setmetatable({}, Dict)
  self._dict = {}
  if type(initial) == 'table' then
    for key, value in pairs(initial) do
      self._dict[key] = value
    end
  end
  return self
end

---@param key string
---@return DictValue
function Dict:get(key)
  return self._dict[key]
end

---@param key string
---@param value? DictValue
function Dict:add(key, value)
  self._dict[key] = value
end

---@param key string
function Dict:remove(key)
  self._dict[key] = nil
end

---@param key string
---@param value? string
function Dict:replace(key, value)
  self._dict[key] = value
end

return Dict
