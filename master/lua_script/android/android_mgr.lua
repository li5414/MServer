-- 机器人管理

local sc = require "command/sc_command"
SC,CS = sc[1],sc[2]

local Android = oo.refer( "android/android" )
local lua_flatbuffers = require "lua_flatbuffers"

local Android_mgr = oo.class( nil,... )

function Android_mgr:__init()
    self.cmd     = {}
    self.android = {}

    for _,v in pairs( SC or {} ) do
        self.cmd[ v[1] ] = v
    end

    self.lfb = lua_flatbuffers()
    self.lfb:load_bfbs_path( "fbs","bfbs" )
end

function Android_mgr:start()
    for pid = 1,1 do
        local android = Android( pid )

        android:connect( "127.0.0.1",10002 )

        self.android[pid] = android
    end
end

function Android_mgr:on_android_kill( pid )
    self.android[pid] = nil
end

-- 指令分发
function Android_mgr:cmd_dispatcher( cmd,android )
    local cfg = self.cmd[cmd]
    if not cfg then
        PLOG( "Android_mgr:cmd_dispatcher no such cmd:%d",cmd )
        return
    end

    if not cfg.handler then
        PLOG( "Android_mgr:cmd_dispatcher no handler found:%d",cmd )
        return
    end

    local errno,pkt = 
        android.conn:sc_flatbuffers_decode( self.lfb,cmd,cfg[2],cfg[3] )
    cfg.handler( android,errno,pkt )
end

-- 注册指令处理
function Android_mgr:cmd_register( cmd_cfg,handler )
    local cfg = self.cmd[cmd_cfg[1]]
    if not cfg then
        PLOG( "Android_mgr:cmd_dispatcher no such cmd:%d",cmd_cfg[1] )
        return
    end

    cfg.handler = handler
end

local android_mgr = Android_mgr()

return android_mgr


