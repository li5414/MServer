-- mysql_test.lua
-- 2016-03-08
-- xzc

-- mysql测试用例

local Mysql   = require "mysql.mysql"

local max_insert = 100000
local Mysql_performance = {}
local create_table_str =
"CREATE TABLE IF NOT EXISTS `perf_test` (\
  `auto_id` INT NOT NULL AUTO_INCREMENT,\
  `id` INT NULL DEFAULT 0,\
  `desc` VARCHAR(45) NULL,\
  `amount` INT NULL DEFAULT 0,\
  PRIMARY KEY (`auto_id`),\
  UNIQUE INDEX `id_UNIQUE` (`id` ASC))\
ENGINE = InnoDB\
DEFAULT CHARACTER SET = utf8\
COLLATE = utf8_general_ci;"

t_describe("sql test", function()
    local mysql
    t_it("sql base test", function()
        t_wait(10000)

        mysql = Mysql()
        local g_setting = require "setting.setting"
        mysql:start(g_setting.mysql_ip, g_setting.mysql_port,
            g_setting.mysql_user, g_setting.mysql_pwd,g_setting.mysql_db,
            function()
                t_print(string.format("mysql(%s:%d) ready ...",
                    g_setting.mysql_ip, g_setting.mysql_port))
                mysql:exec_cmd(create_table_str)
                mysql:exec_cmd("TRUNCATE perf_test")

                -- 插入
                mysql:insert("insert into perf_test (id,`desc`,amount) values \z
                    (1,'base test item >>>>>>>>>>>>>>>>>>>><<<<<<<<<<<<<',1)")
                mysql:insert("insert into perf_test (id,`desc`,amount) values \z
                    (2,'base test item >>>>>>>>>>>>>>>>>>>><<<<<<<<<<<<<',1)")
                -- 更新
                mysql:update("update perf_test set amount = 99999 where id = 1")
                -- 删除
                mysql:exec_cmd("delete from perf_test where id = 2")
                -- 查询
                mysql:select("select * from perf_test", function(ecode, res)
                    t_equal(ecode, 0)
                    vd(res)
                    t_done()
                end)
            end)
    end)

    t_after(function()
        -- mysql:stop()
    end)
end)

--[[
默认配置下：30条/s
当作一个事务，100000总共花4s
innodb_flush_log_at_trx_commit = 0,不用事务，100000总共7s
]]

function Mysql_performance:insert_test()
    print( "start mysql insert test",max_insert )
    g_mysql:exec_cmd( "START TRANSACTION" )

    -- desc是mysql关键字，因此需要加``
    for i = 1,max_insert do
        local str = string.format(
            "insert into item (id,`desc`,amount) values (%d,'%s',%d)",
            i,"just test item",i*10 )
        g_mysql:insert( str )
    end

    g_mysql:exec_cmd( "COMMIT" )
end

function Mysql_performance:update_test()
    print( "start mysql update test" )
    g_mysql:update( "update item set amount = 99999999 where id = 1" )
end

function Mysql_performance:select_test()
    print( "start mysql select test" )
    g_mysql:select( self,self.on_select_test,
        "select * from item order by amount desc limit 50" )
end

function Mysql_performance:on_select_test( ecode,res )
    print( "mysql select return ",ecode )
    vd( res )

    f_tm_stop( "mysql test done" )
end
