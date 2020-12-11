-- lupos hello.lua
_lupos = "\027[31mL\027[32mU\027[33mP\027[35mO\027[36mS\027[0m"
package.path = "?.lua;sd:/sys/?.lua"
dofile("repl.lua")
dofile("fscmds.lua")
dofile("misccmds.lua")
dofile("concmds.lua")
dofile("edit.lua")
repl(_lupos.." \027[36mReady\027[0m.\n\n")
--
