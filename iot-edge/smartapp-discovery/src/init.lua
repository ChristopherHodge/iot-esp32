local Driver = require('st.driver')
local caps = require('st.capabilities')

local discovery = require('discovery')
local server = require('server')

local driver =
  Driver(
    'LAN-multisensor',
    {
      discovery = discovery.start,
      supported_capabilities = {
        caps.presence,
        caps.refresh
      }
    }
  )

server.start(driver)

driver:run()
