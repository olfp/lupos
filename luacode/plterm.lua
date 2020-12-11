-- Copyright (c) 2018 Phil Leblanc  -- see LICENSE file
-- Adapted for circle 
------------------------------------------------------------------------
--[[  
plterm - Pure Lua ANSI Terminal functions - unix only
This module assumes the tty is in raw mode. 
It provides functions based on stty (so available on unix) 
to save, set and restore tty modes.
Module functions:
clear()     -- clear screen
cleareol()  -- clear to end of line
golc(l, c)  -- move the cursor to line l, column c
up(n)
down(n)
right(n)
left(n)     -- move the cursor by n positions (default to 1)
color(f, b, m)  
            -- change the color used to write characters
		(foreground color, background color, modifier)
		see term.colors
hide()
show()      -- hide or show the cursor
save()
restore()   -- save and restore the position of the cursor
reset()     -- reset the terminal (colors, cursor position)
input()     -- input iterator (coroutine-based)
		return a "next key" function that can be iteratively called 
		to read a key (escape sequences returned by function keys 
		are parsed)
rawinput()  -- same, but escape sequences are not parsed.
getcurpos() -- return the current position of the cursor
getscrlc()  -- return the dimensions of the screen 
               (number of lines and columns)
keyname()   -- return a printable name for any key
		- key names in term.keys for function keys,
		- control characters are represented as "^A"
		- the character itself for other keys
tty mode management functions
setrawmode()       -- set the terminal in raw mode
setsanemode()      -- set the terminal in a default "sane mode"
savemode()         -- get the current mode as a string
restoremode(mode)  -- restore a mode saved by savemode()
License: BSD
https://github.com/philanc/plterm
-- just in case, a good ref on ANSI esc sequences:   
https://en.wikipedia.org/wiki/ANSI_escape_code
(in the text, "CSI" is "<esc>[")
]]

-- some local definitions

local con = console -- circle specific console functions
local strf = string.format
local byte, char, rep = string.byte, string.char, string.rep
local yield = coroutine.yield

local repr = function(x) return strf("%q", tostring(x)) end


------------------------------------------------------------------------

local out = io.write

local function outf(...) 
	-- write arguments to stdout, then flush.
	io.write(...); io.flush()
end

-- following definitions (from term.clear to term.restore) are
-- based on public domain code by Luiz Henrique de Figueiredo 
-- http://lua-users.org/lists/lua-l/2009-12/msg00942.html

local term={ -- the plterm module

	out = out,
	outf = outf,
	clear = function() out("\27[H\027[J") end,
	cleareol = function() out("\027[K") end,
	golc = function(l,c) out("\027[",l,";",c,"H") end,
	up = function(n) out("\027[",n or 1,"A") end,
	down = function(n) out("\027[",n or 1,"B") end,
	right = function(n) out("\027[",n or 1,"C") end,
	left = function(n) out("\027[",n or 1,"D") end,
	color = function(f,b,m) 
	    if m then out("\027[",f,";",b,";",m,"m")
	    elseif b then out("\027[",f,";",b,"m")
	    else out("\027[",f,"m") end 
	end,
	-- hide / show cursor
	hide = function() out("\027[?25l") end,
	show = function() out("\027[?25h") end,
	-- save/restore cursor position
  -- save = function() out("\027[s") end,
	save = con.getcpos,
	-- restore = function() out("\027[u") end,
  restore = con.setcpos,
	-- reset terminal (clear and reset default colors)
	reset = function() out("\027[H\027[J") end,
}

term.colors = {
	default = 0,
	-- foreground colors
	black = 30, red = 31, green = 32, yellow = 33, 
	blue = 34, magenta = 35, cyan = 36, white = 37,
	-- backgroud colors
	bgblack = 40, bgred = 41, bggreen = 42, bgyellow = 43,
	bgblue = 44, bgmagenta = 45, bgcyan = 46, bgwhite = 47,
	-- attributes
	reset = 0, normal= 0, bright= 1, bold = 1, reverse = 7,
}

------------------------------------------------------------------------
-- key input

term.keys = { -- key code definitions
	unknown = 0x10000,
	esc = 0x1b,
	del = 0x7f,
	kpad = 0x80,
	kf1 = 0xffff,  -- 0xffff-0
	kf2 = 0xfffe,  -- 0xffff-1
	kf3 = 0xfffd,  -- ...
	kf4 = 0xfffc,
	kf5 = 0xfffb,
	kf6 = 0xfffa,
	kf7 = 0xfff9,
	kf8 = 0xfff8,
	kf9 = 0xfff7,
	kf10 = 0xfff6,
	kf11 = 0xfff5,
	kf12 = 0xfff4,
	kins  = 0xfff3,
	kdel  = 0xfff2,
	khome = 0xfff1,
	kend  = 0xfff0,
	kpgup = 0xffef,
	kpgdn = 0xffee,
	kup   = 0xffed,
	kdown = 0xffec,
	kleft = 0xffeb,
	kright = 0xffea,
	kctlup  = 0xffe9,
	kctldown = 0xffe8,	
	kctlleft  = 0xffe7,
	kctlright = 0xffe6,	
}

local keys = term.keys

--special chars (for parsing esc sequences)
local ESC, LETO, LBR, TIL= 27, 79, 91, 126  --  esc, [, ~

local isdigitsc = function(c) 
	-- return true if c is the code of a digit or ';'
	return (c >= 48 and c < 58) or c == 59
end

--ansi sequence lookup table
local seq = {
	['[A'] = keys.kup,
	['[B'] = keys.kdown,
	['[C'] = keys.kright,
	['[D'] = keys.kleft,

	['[5A'] = keys.kctlup,
	['[5B'] = keys.kctldown,
	['[5C'] = keys.kctlright,
	['[5D'] = keys.kctlleft,

	['[2~'] = keys.kins,
	['[3~'] = keys.kdel,
	['[5~'] = keys.kpgup,
	['[6~'] = keys.kpgdn,
	['[7~'] = keys.khome,  --rxvt
	['[8~'] = keys.kend,   --rxvt
	['[1~'] = keys.khome,  --linux
	['[4~'] = keys.kend,   --linux
	['[11~'] = keys.kf1,
	['[12~'] = keys.kf2,
	['[13~'] = keys.kf3,
	['[14~'] = keys.kf4,
	['[15~'] = keys.kf5,
	['[17~'] = keys.kf6,
	['[18~'] = keys.kf7,
	['[19~'] = keys.kf8,
	['[20~'] = keys.kf9,
	['[21~'] = keys.kf10,
	['[23~'] = keys.kf11,
	['[24~'] = keys.kf12,

	['OP'] = keys.kf1,   --xterm
	['OQ'] = keys.kf2,   --xterm
	['OR'] = keys.kf3,   --xterm
	['OS'] = keys.kf4,   --xterm
	['[H'] = keys.khome, --xterm
	['[F'] = keys.kend,  --xterm

	['[[A'] = keys.kf1,  --linux
	['[[B'] = keys.kf2,  --linux
	['[[C'] = keys.kf3,  --linux
	['[[D'] = keys.kf4,  --linux
	['[[E'] = keys.kf5,  --linux

	['OH'] = keys.khome, --vte
	['OF'] = keys.kend,  --vte
	
}

local getcode = function() return byte(io.read(1)) end

term.input = function()
	-- return a "read next key" function that can be used in a loop
	-- the "next" function blocks until a key is read
	-- it returns ascii code for all regular keys, or a key code
	-- for special keys (see term.keys)
	-- (this function assume the tty is already in raw mode)
	return coroutine.wrap(function()
		local c, c1, c2, ci, s
		while true do
			c = getcode()
			if c ~= ESC then -- not a seq, yield c
				yield(c) 
				goto continue
			end 
			c1 = getcode()
			if c1 == ESC then -- esc esc [ ... sequence
				yield(ESC)
				-- here c still contains ESC, read a new c1
				c1 = getcode() -- and carry on ...
			end
			if c1 ~= LBR and c1 ~= LETO then -- not a valid seq
				yield(ESC) ; yield(c1) -- yield ESC c1
				goto continue
			end
			c2 = getcode()
			s = char(c1, c2)
			if c2 == LBR then -- esc[[x sequences (F1-F5 in linux console)
				s = s .. char(getcode())
			end
			if seq[s] then 
				yield(seq[s])
				goto continue
			end
			if not isdigitsc(c2) then
				yield(c) ; yield(c1) ; yield(c2)
				goto continue
			end
			while true do
				ci = getcode()
				s = s .. char(ci)
				if ci == TIL then 
					if seq[s] then
						yield(seq[s])
						goto continue
					else
						-- valid but unknown sequence - ignore it
						yield(keys.unknown)
						goto continue
					end
				end
				if not isdigitsc(ci) then
          if isdigitsc(c2) then
            if seq[s] then
              yield(seq[s])
              goto continue
            end
          end
					-- not a valid seq. return all the chars
					yield(ESC)
					for i = 1, #s do yield(byte(s, i)) end
					goto continue
				end
			end--while
			-- assume c is a regular char, return its ascii code
			::continue::
		end
	end)--coroutine
end--input()

term.rawinput = function()
	-- return a "read next key" function that can be used in a loop
	-- the "next" function blocks until a key is read
	-- it returns ascii code for all keys
	-- (this function assume the tty is already in raw mode)
	return coroutine.wrap(function()
		local c
		while true do
			c = getcode()
			yield(c) 
		end
	end)--coroutine
end--rawinput()

term.getcurpos = con.getcursor
	-- return current cursor position (line, column as integers)

term.getscrlc = con.size
	-- return current screen dimensions (line, coloumn as integers)

term.keyname = function(c)
	for k, v in pairs(keys) do 
		if c == v then return k end
	end
	if c < 32 then return "^" .. char(c+64) end
	if c < 256 then return char(c) end
	return tostring(c)
end

------------------------------------------------------------------------
-- circle tty mode management

term.setrawmode = function()
  return con.mode(0)
end

term.setsanemode = function()
  return con.mode("sane")
end

term.savemode = function()
  return con.mode()
end

term.restoremode = function(mode)
  return con.mode(mode)
end

return term 

