-- clientnet.lua
-- 2015-09-14
-- xzc

-- 单个客户端连接

local net_mgr = require "lua.net.netmgr"

local Client_net = oo.class ( nil,... )

-- 构造函数
function Client_net:__init( fd )
    self.fd = fd
end

-- 断开事件
function Client_net:on_disconnect()
    net_mgr:pop( self )
    PLOG( "i am quit %d",self.fd )
end

-- 读事件
function Client_net:on_read( pkt )
    print( pkt,"quit now " .. self.fd )
    ev:io_kill( self.fd )
end

return Client_net
