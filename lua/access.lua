-- Minimal access phase handler - just log, don't interfere
ngx.log(ngx.DEBUG, "lua access handler started")

-- Only do expensive operations for specific endpoints
if ngx.var.uri:match("^/lua/") then
    ngx.log(ngx.DEBUG, "lua access handler: special endpoint detected")
end
