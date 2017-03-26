-- 客户端网络连接

local command_mgr = require "command/command_mgr"
local network_mgr = require "network/network_mgr"

local Clt_conn = oo.class( nil,... )

function Clt_conn:__init( conn )
    self.auth = false
    self.beat = 0
    self.fchk = 0 -- fail check

    conn:set_self_ref( self )
    conn:set_on_disconnect( self.on_disconnected  )
    conn:set_on_command( self.on_unauthorized_cmd )

    self.conn = conn
end

-- 处理未认证之前发的指令
function Clt_conn:on_unauthorized_cmd()
    local cmd = self.conn:clt_next()
    while cmd and not self.auth do
        command_mgr:clt_unauthorized_cmd( cmd,self )

        cmd = self.conn:clt_next()
    end

    if cmd then self:on_command() end
end

-- 网络协议回调
function Clt_conn:on_command()
    local cmd = self.conn:clt_next()
    while cmd do
        command_mgr:clt_invoke( cmd,self )

        cmd = self.conn:clt_next()
    end
end

-- 连接断开处理
function Clt_conn:on_disconnected()
    return network_mgr:clt_disconnect( self )
end

-- 认证成功
function Clt_conn:authorized( pkt )
    self.auth = true
    self.conn:set_on_command( self.on_command )
end

-- 关闭连接(TODO: 暂时用设置nil方式释放引用)
function Clt_conn:close()
    self.conn = nil
end
