-- Content handler that creates subrequests
local results = {}

-- Make several subrequests to trigger internal request handling
table.insert(results, "Subrequest 1 status: " .. ngx.location.capture("/healthz").status)
ngx.sleep(0.001)
table.insert(results, "Subrequest 2 status: " .. ngx.location.capture("/healthz").status)
ngx.sleep(0.001)
table.insert(results, "Subrequest 3 status: " .. ngx.location.capture("/healthz").status)

-- Output results
ngx.say("Subrequest test completed")
for _, result in ipairs(results) do
    ngx.say(result)
end
