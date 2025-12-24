-- Body filter to capture response body
ngx.log(ngx.DEBUG, "lua capture body filter, uri:", ngx.var.uri)

-- Get the response body chunk
local chunk = ngx.arg[1]
if chunk then
    ngx.ctx.body_chunks = ngx.ctx.body_chunks or {}
    table.insert(ngx.ctx.body_chunks, chunk)
end

-- Check if this is the last chunk
if ngx.arg[2] then
    if ngx.ctx.body_chunks then
        local body = table.concat(ngx.ctx.body_chunks)
        ngx.log(ngx.DEBUG, "lua captured body size: ", #body)
    end
end
