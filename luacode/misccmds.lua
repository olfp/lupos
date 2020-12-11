-- lupos misc cmds
function unload(name)
  package.loaded[name] = nil
end
function reload(name)
  package.loaded[name] = nil
  _G[name] = require(name)
end
function run(name) 
  dofile(name .. ".lua")
end
function list(file)
	local f = assert(io.open(file, "rb"))
    	local content = f:read("*all")
    	f:close()	
	print(content)
end
oprint = print
function print(t)
  if(type(t) == "table") then
    for x, y in pairs(t) do
      oprint(x, y)
    end
  else
    oprint(t)
  end
end
