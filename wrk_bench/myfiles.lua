-- wrk -t4 -c100 -d30s -s myfiles.lua https://localhost/api/myfiles
wrk.method = "POST"
wrk.headers["Content-Type"] = "application/json"

local counter = 0

function request()
    counter = counter + 1
    wrk.body = string.format('{"username":"testuser%d"}', counter % 100)
    return wrk.format(nil, "/api/myfiles")
end
