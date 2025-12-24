-- Balancer module for load balancing
local balancer = require "ngx.balancer"

ngx.log(ngx.DEBUG, "lua balancer: get peer, tries: 1")

-- Get backend server details
local host = "172.23.0.10"  -- Static backend IP address (configured in docker-compose.yml)
local port = 3000

-- Set the backend server
local ok, err = balancer.set_current_peer(host, port)
if not ok then
    ngx.log(ngx.ERR, "failed to set current peer: ", err)
    return ngx.exit(500)
end

ngx.log(ngx.DEBUG, "lua balancer: peer set to ", host, ":", port)

-- Set timeouts
balancer.set_timeouts(5, 60, 60)  -- connect, send, read timeouts

ngx.log(ngx.DEBUG, "lua balancer: peer selected successfully")
