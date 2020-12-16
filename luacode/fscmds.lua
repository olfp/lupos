-- lupos fs commands
fs=filesystem
path=fs.getpath
if not io.rawopen then
  io.rawopen=io.open
  io.open = function(fname, ...)
    fs.checkdir(fname)
    return io.rawopen(fname, ...)
  end
end
function fsparse(pathplus, maskdef, fromto)
  local pathplus = pathplus or ""
  local fs = filesystem
  local start, opts = pathplus:match("^([^?]*)%??([^\\/]-)$")
  start = start or ""
  local ds = fs.getstat(start)
  if ds and ds.attributes and ((ds.attributes & 0x10) > 0) then
    -- user is trying to list a folder
    start = start .. "/"
  end
  local target
  if start:find(">") then
    start, target = start:match("^([^>]*)>([^>]*)$")
    start = start or ""
    target = target or ""
  end
  local path, slash, mask = start:match("^(.-)([\\/]?)([^:\\/]*)$")
  path = path or ""
  mask = mask or ""
  opts = opts or ""
  if #mask == 0 then
    mask = maskdef
  end
  if #path == 0 and #slash > 0 then
    path = "/"
  end
  --print("Start: "..start.." Path: "..path.." Slash: "..slash.." Mask: "..mask.." Opts: "..opts)
  local pattern = mask
  if pattern then 
    pattern = "^"..pattern:gsub("%.", "%%."):gsub("%*", ".*").."$"
  end
  --print("Path: "..path.." Mask: "..mask.." Opts: "..opts)
  local dir, msg = fs.getdir(path)
  if not dir then
    --io.stderr:write("Error:"..msg.."\n")
    return nil
  end
  if target then
    source = path
    path = { source, target }
  end
  return dir, path, mask, pattern, opts
end
function free(pathplus)
  local pathplus = pathplus or ""
  local fs = filesystem
  local out = ""
  local path, opts = pathplus:match("^([^?]*)%??([^\\/]-)$")
  path = path or ""
  local savepath = fs.getpath()
  local blks = fs.getfree(path)
  if blks then
    if opts:find("b") then
	out = "Free space on "..path.." "..tostring(math.floor(blks*512)).." Bytes"
    elseif opts:find("k") then
	out = "Free space on "..path.." "..tostring(math.floor(blks//2)).." kB"
    elseif opts:find("m") then
	out = "Free space on "..path.." "..tostring(math.floor(blks//2048)).." MB"
    else
	out = tostring(math.floor(blks))
    end
  else
    out = "Error geting free blocks from '"..path.."'"
  end
  go(savepath)
  return (out:gsub("^(.-)%s*$", "%1"))
end
function volumes(arg)
  local out = ""
  if arg == "??" then
    out = [[
volumes: list available volumes
]]
  else 
    local vollist = fs.getvols()
    if vollist then
      for i, vol in ipairs(vollist) do
        local f = fs.getfree(vol..":")
        if f then
          out = out..vol.."\n"
        end
      end
    else
      out = "No volumes found."
    end
  end
  return (out:gsub("^(.-)%s*$", "%1"))
end
function folder(pathplus)
  local pathplus = pathplus or ""
  local fs = filesystem
  local out = ""
  local path, opts = pathplus:match("^([^?]*)%??([^\\/]-)$")
  if opts:find("?") then
    out = [[
folder: create a new folder
Examples: folder "myfolder" ; folder "my/folder"
Options after ?:
	p - create all folders in path if needed (not implemented)
]]
  else 
    path = path or ""
    out = fs.mkdir(path)
    out = out or "Could not create folder '"..path.."'"
  end
  return (out:gsub("^(.-)%s*$", "%1"))
end
dofile("/sys/del.lua")
dofile("/sys/copy.lua")
dofile("/sys/rename.lua")
ren=rename
dofile("/sys/dir.lua")
dofile("/sys/go.lua")
function up() return go("..") end
return "ok"
-- EOF
