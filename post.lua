wrk.method = "POST"
wrk.body   = string.rep("x", 100)
wrk.headers["Content-Type"] = "text/plain"
