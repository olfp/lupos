-- lupos hello.lua
_lupos = "\027[31mL\027[32mU\027[33mP\027[35mO\027[36mS\027[0m"
package.path = "?.lua;sd:/sys/?.lua"
dofile("/sys/repl.lua")
dofile("/sys/fscmds.lua")
dofile("/sys/misccmds.lua")
dofile("/sys/concmds.lua")
dofile("/sys/edit.lua")
repl(_lupos.." \027[36mReady\027[0m.\n\n")
con.mode("sane")
--eof
--
