-- wrk -t4 -c100 -d30s -s md5check.lua https://localhost/api/md5
wrk.method = "POST"
wrk.headers["Content-Type"] = "application/json"

local md5list = {
    "d41d8cd98f00b204e9800998ecf8427e",
    "e99a18c428cb38d5f260853678922e03",
    "5d41402abc4b2a76b9719d911017c592",
    "098f6bcd4621d373cade4e832627b4f6",
    "ad0234829205b9033196ba818f7a872b",
}

local counter = 0

function request()
    counter = counter + 1
    local md5 = md5list[(counter % #md5list) + 1]
    wrk.body = string.format('{"username":"testuser0","md5":"%s","filename":"bench_%d.dat"}', md5, counter)
    return wrk.format(nil, "/api/md5")
end
