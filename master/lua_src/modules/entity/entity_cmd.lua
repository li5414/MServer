-- entity_cmd.lua
-- xzc
-- 2018-12-01

-- 实体指令入口

-- 热更
local function hot_fix( srv_conn,pkt )
    local hf = require "http.www.hot_fix"
    hf:fix( pkt.module or {} )
end

-- 同步gw的玩家属性到area
local function player_update_base( pid,base,new )
    local player = nil

    -- 首次进入场景，需要创建实体
    if new then
        player = g_entity_mgr:new_entity(ET.PLAYER,pid)
        PLOG("area player update base:",pid)
    else
        player = g_entity_mgr:get_player(pid)
    end

    if not player then
        ELOG("update_player_base no player found",pid)
        return
    end

    return player:update_base_info(base)
end

-- 同步gw的玩家战斗属性
local function player_update_battle_abt( pid,abt_list )
    local player = g_entity_mgr:get_player(pid)
    if not player then
        ELOG("player_update_battle_abt no player found",pid)
        return
    end

    return player:update_battle_abt(abt_list)
end

-- 玩家退出area
local function player_exit( pid )
    g_entity_mgr:del_entity_player( pid )
    PLOG("area player exit:",pid)
end

-- 玩家进入场景
local function player_enter_scene( pid,dungeon_id,scene_id )
    local player = g_entity_mgr:get_player(pid)
    if not player then
        ELOG("player_enter_scene no player found",pid)
        return
    end

    PLOG("player enter scene",pid)
end

g_rpc:declare( "player_exit",player_exit )
g_rpc:declare( "player_enter_scene",player_enter_scene )
g_rpc:declare( "player_update_base",player_update_base )
g_rpc:declare( "player_update_battle_abt",player_update_battle_abt )