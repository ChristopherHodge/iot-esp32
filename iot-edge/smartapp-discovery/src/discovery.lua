local socket = require('socket')
local http   = require('socket.http')
local log    = require('log')
local mdns   = require('mdns')
local config = require('config')

local function update_device(device)
  local ip = device.ipv4
  local port = device.port

  local url = string.format(
    "http://%s:%s/register?apiurl=%s&apikey=%s", ip, port, config.APIURL, config.APIKEY
  )

  for i=1,3 do
    local body, code, headers, status = http.request({
      url = url,
      method = "PUT",
      create = function()
        local sock = socket.tcp()
        sock:settimeout(5)
        return sock
      end
    })

    if not body and code ~= "timeout" then
      error(string.format("error while registering device: %s",
                          status))
    elseif code ~= 200 then
       error(string.format("unexpected HTTP error response: %s",
                           status))
    elseif code == 200 then
      break
    end
  end
end


local function search()
  log.info('starting mdns query')
  local res = mdns.query('_smartthings._tcp.local')
  local device = {}
  local cnt = 0
  if (res) then
    for k,v in pairs(res) do
      cnt = cnt + 1
      log.info(k) 
      for k1,v1 in pairs(v) do
        log.info('  '..k1..': '..v1)
        device[k1] = v1
      end
    end
  end
  if (cnt > 0) then
    return device
  end
end


local discovery = {}
function discovery.start(driver, opts, cons)
  while true do
    local device = search()
    if (device) then
      update_device(device)
      return "Success"
    end
  end
end
return discovery
