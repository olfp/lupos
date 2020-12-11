-- lupos fs commands
fs=filesystem
path=fs.getpath
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
dofile("del.lua")
dofile("dir.lua")
dofile("go.lua")
function up() return go("..") end
return "ok"
-- EOF
