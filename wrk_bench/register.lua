-- wrk -t4 -c100 -d30s -s register.lua https://localhost/api/register
wrk.method = "POST"
wrk.headers["Content-Type"] = "application/json"

local counter = 0
local tid = 0

function setup(thread)
    thread:set("id", tid)
    tid = tid + 1
end

function init(args)
    id = wrk.thread:get("id")
end

function request()
    counter = counter + 1
    local uid = string.format("bench_t%d_%d", id, counter)
    wrk.body = string.format('{"username":"%s","password":"bench123456","nickname":"nick_%s"}', uid, uid)
    return wrk.format(nil, "/api/register")
end

