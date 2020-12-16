-- Copyright (c) 2019  Phil Leblanc  -- see LICENSE file

------------------------------------------------------------------------
--[[  ple - a Pure Lua Editor

editor actions:	see table editor.action
key bindings:	see table editor.bindings

configuration:
The editor can be customized with a Lua file loaded at initialization. 
The configuration file is looked for in sequence at the following locations:
	- the file which pathname is in the environment variable PLE_INIT
	- ./ple_init.lua
	- ~/.config/ple/ple_init.lua
The first file found, if any, is loaded. 

(see https://github.com/philanc/ple  -  License: MIT)

]]

-- some local definitions (used by the module term and/or by the editor

local strf = string.format
local byte, char, rep = string.byte, string.char, string.rep
local yield = coroutine.yield

local repr = function(x) return strf("%q", tostring(x)) end
local function max(x, y) if x < y then return y else return x end end 
local function min(x, y) if x < y then return x else return y end end 

local function pad(s, w) -- pad a string to fit width w
	if #s >= w then return s:sub(1,w) else return s .. rep(' ', w-#s) end
end

local function lines(s)
	-- split s into a list of lines
	lt = {}
	s = s .. "\n"
	for l in string.gmatch(s, "(.-)\r?\n") do table.insert(lt, l) end
	return lt
end	
	
local function readfile(fn)
	-- read file with filename 'fn' as a list of lines
	local fh, errm = io.open(fn)
	if not fh then return nil, errm end
	local ll = {}
	for l in fh:lines() do table.insert(ll, l) end
	fh:close()
	return ll
end

local function fileexists(fn)
	-- check if file 'fn' exists and can be read. return true or nil
	if not fn then return nil end
	local fh = io.open(fn)
	if not fh then return nil end	
	fh:close()
	return true
end

local term = require "plterm"


------------------------------------------------------------------------
-- EDITOR


-- some local definitions

local go, cleareol, color = term.golc, term.cleareol, term.color
local out, outf = term.out, term.outf
local col, keys = term.colors, term.keys
local flush = io.flush

	
------------------------------------------------------------------------
-- global objects and constants

local tabln = 8
local EOL = char(187) -- >>, indicate more undisplayed chars in s
local NDC = char(183) -- middledot, used for non-displayable latin1 chars
local EOT = '~'  -- used to indicate that we are past the end of text

-- editor is the (not any more global editor object
local editor = {}
function editor.init()
  editor.quit = false           -- set to true to quit editor_loop()
  editor.nextk = term.input()   -- the "read next key" function
  editor.keyname = term.keyname -- return the displayable name of a key
  editor.buflist = {}           -- list of buffers
  editor.bufindex = 0           -- index of current buffer
  -- buf is the current buffer
  -- this is the same object as editor.buflist[editor.bufindex]
  editor.buf = {}  
end

-- style functions

editor.style = {
	normal = function() color(col.normal) end, 
	status = function() color(col.red, col.bold) end, 
	msg = function() color(col.normal); color(col.green) end, 
	sel = function() color(col.magenta, col.bold) end, 
	bckg = function() color(col.black, col.bgyellow) end, 
}

local style = editor.style


-- dialog functions

function editor.msg(m)
	-- display a message m on last screen line
	m = pad(m, editor.scrc)
	go(editor.scrl, 1); cleareol(); style.msg()
	out(m); style.normal(); flush()
end

function editor.readstr(prompt)
	-- display prompt, read a string on the last screen line
	-- [read only ascii or latin1 printable chars - no tab]
	-- [ no edition except bksp ]
	-- if ^G then return nil
	local s = ""
	editor.msg(prompt)
	while true do
		go(editor.scrl, #prompt+1); cleareol(); outf(s)	-- display s
		k = editor.nextk()
		if (k >= 32 and k <127) or (k >=160 and k < 256) then
			s = s .. char(k) 
		elseif k == 8 or k == keys.del then -- backspace
			s = s:sub(1, -2)
		elseif k == 10 then return s  -- lf/return
		elseif k == 13 then return s  -- cr/return
		elseif k == 7 then return nil -- ^G - abort
		else -- ignore all other keys
		end
	end--while
end --readstr

function editor.readchar(prompt, charpat)
	-- display prompt on the last screen line, read a char
	-- if ^G then return nil
	-- return the key as a char only if it matches charpat
	-- ignore all non printable ascii keys and non matching chars
	editor.msg(prompt)
	editor.redisplay(editor.buf) -- ensure cursor stays in buf
	while true do
		k = editor.nextk()
		if k == 7 then return nil end -- ^G - abort
		if (k < 127) then
			local ch = char(k)
			if ch:match(charpat) then return ch end
		end
		-- ignore all other keys
	end--while
end --readkey

function editor.status(m)
	-- display a status string on top screen line
	m = pad(m, editor.scrc)
	go(1, 1); cleareol(); style.status()
	out(m); style.normal(); flush()
end

local dbgs = ""
function editor.dbg(s)
	dbgs = s or ""
end
	

function editor.statusline()
	local s = strf("cur=%d,%d ", editor.buf.ci, editor.buf.cj)
	if editor.buf.si then s = s .. strf("sel=%d,%d ", editor.buf.si, editor.buf.sj) end
	-- uncomment the following for debug purposes
--~ 	s = s .. strf("li=%d ", editor.buf.li)
--~ 	s = s .. strf("hs=%d ", editor.buf.hs)
--~ 	s = s .. strf("ual=%d ", #editor.buf.ual)
--~ 	s = s .. strf("editor.buf=%d ", editor.bufindex)
	s = s .. strf(
		"[%s] (%s) %s -- Help: ^X^H -- %s", 
		editor.buf.filename or "unnamed", 
		editor.buf.unsaved and "*" or "", 
		editor.tabspaces and "SP" or "TAB", 
		dbgs)
	return s
end--statusline

local dbg = editor.dbg

------------------------------------------------------------------------
-- screen display functions  (boxes, line display)

local function boxnew(x, y, l, c)
	-- a box is a rectangular area on the screen
	-- defined by top left corner (x, y) 
	-- and number of lines and columns (l, c)
	local b = {x=x, y=y, l=l, c=c}
	b.clrl = rep(" ", c) -- used to clear box content
	return b
end

local function boxfill(b, ch, stylefn)
	local filler = rep(ch, b.c)
	stylefn()
	for i = 1, b.l do
		go(b.x+i-1, b.y); out(filler)
	end
	style.normal() -- back to normal style
	flush()
end

-- line display

local function ccrepr(b, j)
	-- return display representation of char with code b
	-- at line offset j (j is used for tabs)
	local s
	if b == 9 then s = rep(' ', tabln - j % tabln)
	elseif (b >= 127 and b <160) or (b < 32) then s = NDC
	else s = char(b)
	end--if
	return s
end --ccrepr

local function boxline(b, hs, bl, l, insel, jon, joff)
	-- display line l at the bl-th line of box b, 
	-- with horizontal scroll hs
	-- if s is tool long for the box, return the
	-- index of the first undisplayed char in l
	-- insel: true if line start is in the selection
	-- jon: if defined and not insel, position of beg of selection
	-- joff: if defined, position of end of selection
	local bc = b.c
	local cc = 0 --curent col in box
	-- clear line (don't use cleareol - box maybe smaller than screen)
	go(b.x+bl-1, b.y); out(b.clrl)  
	go(b.x+bl-1, b.y)
	if insel then style.sel() end
	for j = 1, #l do
		if (not insel) and j == jon+1 then style.sel(); insel=true end
		if insel and j == joff+1 then style.normal() end
		local chs = ccrepr(byte(l, j), cc)
		cc = cc + #chs
		if cc >= bc + hs then 
			go(b.x+bl-1, b.y+b.c-1)
			outf(EOL)
			style.normal()
			return j -- index of first undisplayed char in s
		end
		if cc > hs then out(chs) end
	end
	style.normal()
end --boxline

------------------------------------------------------------------------
-- screen redisplay functions


local function adjcursor(buf)
	-- adjust the screen cursor so that it matches with 
	-- the buffer cursor (buf.ci, buf.cj)
	--
	-- first, adjust buf.li
	local bl = buf.box.l
	if buf.ci < buf.li or buf.ci >= buf.li+bl then 
		-- cursor has moved out of box.
		-- set li so that ci is in the middle of the box
		buf.li = max(1, buf.ci-bl//2) 
		buf.chgd = true
	end
	local cx = buf.ci - buf.li + 1
	local cy = 1 -- box column index, ignoring horizontal scroll
	local col = buf.box.c
	local l = buf.ll[buf.ci]
	for j = 1, buf.cj do
		local b = byte(l, j)
		if not b then break end
		if b == 9 then cy = cy + (tabln - (cy-1) % tabln)
		else cy = cy + 1 
		end
--~ 		if cy > col then --don't move beyond the right of the box
--~ 			cy = col
--~ 			buf.cj = j
--~ 			break
--~ 		end
	end
	-- determine actual hs
	local hs = 0 -- horizontal scroll
	local cys = cy -- actual box column index
	while true do
		if cys >= col then 
			cys = cys - 40
			hs = hs + 40
		else
			break
		end
	end--while
	if hs ~= buf.hs then
		buf.chgd = true
		buf.hs = hs
	end
	if buf.chgd then return end -- (li or hs or content change)
	-- here, assume that cursor will move within the box
	editor.status(editor.statusline()) 
	go(buf.box.x + cx - 1, buf.box.y + cys - 1); flush()
end -- adjcursor


local function displaylines(buf)
	-- display buffer lines starting at index buf.li 
	-- in list of lines buf.ll
	local b, ll, li, hs = buf.box, buf.ll, buf.li, buf.hs
	local ci, cj, si, sj = buf.ci, buf.cj, buf.si, buf.sj
	local bi, bj, ei, ej -- beginning and end of selection
	local sel, insel, jon, joff = false, false, -1, -1
	if si then -- set beginning and end of selection
		sel = true
		if si < ci then bi=si; bj=sj; ei=ci; ej=cj
		elseif si > ci then bi=ci; bj=cj; ei=si; ej=sj
		elseif sj < cj then bi=si; bj=sj; ei=ci; ej=cj
		elseif sj >= cj then bi=ci; bj=cj; ei=si; ej=sj
		end
	end
	for i = 1, b.l do
		local lx = li+i-1
		if sel then
			insel, jon, joff = false, -1, -1
			if lx > bi and lx < ei then insel=true
			elseif lx == bi and lx == ei then jon=bj; joff=ej
			elseif lx == bi then jon=bj
			elseif lx == ei then joff=ej; insel=true
			end
		end
		local l = ll[lx] or EOT
		boxline(b, hs, i, l, insel, jon, joff)
	end
	flush()
end

local function redisplay(buf)
	-- adjust cursor and repaint buffer lines if needed
	adjcursor(buf)
	if buf.chgd or buf.si then
		displaylines(buf)
		buf.chgd = false
		adjcursor(buf)
	end
	buf.chgd = false
end --redisplay


function editor.fullredisplay()
	-- complete repaint of the screen
	-- performed initially, or when a new buffer is displayed, or
	-- when requested by the user (^L command) because the 
	-- screen is garbled or the screensize has changed (xterm, ...)
	--
	editor.scrl, editor.scrc = term.getscrlc()
	-- [TMP!! editor.scrbox is a bckgnd box with a pattern to
	-- visually check that edition does not overflow buf box
	-- (this is for future multiple windows, including side by side)]
	editor.scrbox = boxnew(1, 1, editor.scrl, editor.scrc)
--~ 	-- debug layout
--~ 	boxfill(editor.scrbox, ' ', style.bckg)
--~ 	buf.box = boxnew(2, 2, editor.scrl-3, editor.scrc-2)
	--
	-- regular layout
	boxfill(editor.scrbox, ' ', style.normal)
	editor.buf.box = boxnew(2, 1, editor.scrl-2, editor.scrc)
	editor.buf.chgd = true
	redisplay(editor.buf)
end --fullredisplay

editor.redisplay = redisplay

------------------------------------------------------------------------
-- BUFFER AND CURSOR MANIPULATION 
--
-- 		use these functions instead of direct buf.ll manipulation. 
-- 		This will make it easier to change or enrich the 
-- 		representation later. (eg. syntax coloring, undo/redo, ...)

buffer = {}; buffer.__index = buffer --buffer class

-- (note: 'b' is a buffer object in all functions below)

	
function buffer.new(ll)
	-- create and initialize a new buffer object
	-- ll is a list of lines
	local b = { 
	  ll=ll,	-- list of text lines
	  ci=1, cj=0,	-- text cursor (line ci, offset cj)
	  li=1,		-- index in ll of the line at the top of the box
	  hs=0,		-- horizontal scroll (number of columns)
	  chgd=true,	-- true if buffer has changed since last display
	  unsaved=false,-- true if buffer content has changed since last save
	  ual = {},	-- undo action list
	  ualtop = 0,	-- current top of the undo list (~= #ual !! see undo)
	  -- box: a rectangular region of the screen where the buffer 
	  --      is displayed (see boxnew() above)
	  --      the box is assigned to the buffer by a layout function.
	  --      for the moment, the layout is performed by the 
	  --      fullredisplay() function.
	  bindings = {},	-- action table for this buffer (used by modes)
	}
	setmetatable(b, buffer)
	return b
end--new


-- various predicates and accessors

-- test if at end / beginning of  line  (eol, bol)
function buffer.ateol(b) return b.cj >= #b.ll[b.ci] end
function buffer.atbol(b) return b.cj <= 0 end
-- test if at  first or last line of text
function buffer.atfirst(b) return (b.ci <= 1) end
function buffer.atlast(b) return (b.ci >= #b.ll) end
-- test if at  end or beginning of  text (eot, bot)
function buffer.ateot(b) return b:atlast() and b:ateol() end
function buffer.atbot(b) return b:atfirst() and b:atbol() end

function buffer.markbeforecur(b)
	return (b.si < b.ci) or (b.si == b.ci and b.sj < b.cj)
end

function buffer.getcur(b) return b.ci, b.cj end
function buffer.getsel(b) return b.si, b.sj end

function buffer.geteol(b) 
	-- return coord of current end of line
	local ci = b.ci
	return ci, #b.ll[ci]
end

function buffer.getline(b, i)
	-- return current line and cursor position in line
	-- if i is provided, return line i
	if i then return b.ll[i], 1 end
	return b.ll[b.ci], b.cj
end

function buffer.getlines(b, di, dj)
	-- return the text between the cursor and point (di, dj) as
	-- a list of lines. this assumes that (di, dj) is after the cursor.
	local ci, cj = b:getcur()
	if di == ci then
		return { b.ll[ci]:sub(cj+1, dj) }
	end
	local sl = {}
	for i = ci, di do
		local l = b.ll[i]
		if i == ci then l = l:sub(cj+1) end
		if i == di then l = l:sub(1, dj) end
		table.insert(sl, l)
	end
	return sl
end--getlines

function buffer.gettext(b)
	return table.concat(b.ll, '\n')
end

-- cursor movement

function buffer.setcurj(b, j) -- set cursor on the current line
	local ci = b:getcur()
	local ln = #b.ll[ci]
	if not j or j > ln then j = ln end
	if j < 0 then j = 0 end
	b.cj = j
	return j
end
		
function buffer.setcur(b, i, j) -- set cursor absolute
	if not i or i > #b.ll then i = #b.ll end
	if i < 1 then i = 1 end
	if not j or j > #b.ll[i] then j = #b.ll[i] end
	if j < 0 then j = 0 end
	b.ci, b.cj = i, j
	return i, j
end

-- modification at cursor
-- all modifications should be performed by the following functions:
--   bufins(strlist)
--	insert list of string at cursor. if strlist is a string, it is
--	equivalent to a list with only one element 
--	if buffer contains one line "abc" and cursor is between b and c
--	(ie screen cursor is on 'c') then 
--	bufins{"xx"}  changes the buffer line to "abxxc"
--	bufins{"xx", "yy"}  now the buffer has two lines: "abxx", "yyc"
--	bufins{"", ""}  inserts a newline:   "ab", "c"
--
--   bufdel(di, dj)
--	delete all characters between the cursor and point (di, dj)
--	(bufdel assumes that di, dj is after the cursor)
--	if the buffer is  ("abxx", "yyc") and the cursor is just after 'b',
--	bufdel(2,2) changes the buffer to ("abc")
		

local ualpush -- defined further down with all undo functions

function buffer.bufins(b, sl, no_undo)
	-- if no_undo is true, don't record the modification
	local slc = {} -- dont push directly sl. make a copy.
	if type(sl) == "string" then 
		slc[1] = sl
	else
		for i = 1, #sl do slc[i] = sl[i] end
	end
	if not no_undo then ualpush(b, 'ins', slc) end
	local ci, cj = b:getcur()
	local l = b.ll[ci]
	local l1, l2 = l:sub(1,cj), l:sub(cj+1)
	local s1 = nil
	if type(sl) == "string" then	s1 = sl
	elseif #sl == 1 then  s1 = sl[1]
	end
	if s1 then -- insert s1 in current line
		b.ll[ci] = l1 .. s1 .. l2
		b:setcur(ci, cj + #s1)
	else -- several lines in sl
		b.ll[ci] = l1 .. sl[1]
		ci = ci + 1
		for i = 2, #sl-1 do
			table.insert(b.ll, ci, sl[i])
			ci = ci + 1
		end
		local last = sl[#sl]
		cj = #last
		table.insert(b.ll, ci, last .. l2)
		b:setcur(ci, cj)
	end
	b.chgd = true
	b.unsaved = true
	return true
end--bufins

function buffer.bufdel(b, di, dj, no_undo)
	-- if no_undo is true, don't record the modification
	if not no_undo then ualpush(b, 'del', b:getlines(di, dj)) end
	local ci, cj = b:getcur()
	local l1, l2 = b.ll[ci]:sub(1,cj), b.ll[di]:sub(dj+1)
	if di == ci then -- delete in current line at cursor
		b.ll[ci] = l1 .. l2
	else -- delete several lines
		local ci1 = ci + 1
		for i = ci1, di do
			-- the next line to remove is always the line at ci+1
			table.remove(b.ll, ci1) 
		end
		b.ll[ci] = l1 .. l2
	end
	b.chgd = true
	b.unsaved = true
	return true
end--bufdel

function buffer.settext(b, txt)
	-- replace the buffer text 
	-- !! it cannot be undone and it clears the undo stack !!
	-- the cursor and display are reinitialized at the top 
	-- of the new text.
	buffer.undo_clearall(b)
	b.ll = lines(txt)
	b.chgd = true
	b.unsaved = true
	b.ci = 1 -- line index
	b.cj = 0 -- cursor offset
	b.li = 1 -- index of line at the top of the box
	b.hs = 0 -- horizontal scroll
	return true
end--settext

------------------------------------------------------------------------
-- undo functions  

function ualpush(b, op, sl)
	-- push enough context to be able to undo a core operation (ins, del)
	-- sl is always a list of lines
	local top = #b.ual
	if top > b.ualtop then -- remove the remaining redo actions
		for i = top, b.ualtop+1, -1 do table.remove(b.ual, i) end
		assert(#b.ual == b.ualtop)
	end
	local last = b.ual[top]
	
	-- try to merge successive insch()

	sl.op, sl.ci, sl.cj = op, b.ci, b.cj
	table.insert(b.ual, sl)
	b.ualtop = b.ualtop + 1
	return
end

function buffer.op_undo(b, sl)
	b:setcur(sl.ci, sl.cj)
	if sl.op == "del" then 
		return b:bufins(sl, true)
	elseif sl.op == "ins" then
		return b:bufdel(sl.ci+#sl-1, sl.cj+#sl[#sl], true)
	else
		return nil, "unknown op"
	end
end

function buffer.op_redo(b, sl)
	b:setcur(sl.ci, sl.cj)
	if sl.op == "ins" then 
		return b:bufins(sl, true)
	elseif sl.op == "del" then
		return b:bufdel(sl.ci+#sl-1, sl.cj+#sl[#sl], true)
	else
		return nil, "unknown op"
	end
end

function buffer.undo_clearall(b)
	b.ual = {}
	b.ualtop = 0	
end


------------------------------------------------------------------------
-- EDITOR ACTIONS

editor.actions = {}
local e = editor.actions

local msg, readstr, readchar = editor.msg, editor.readstr, editor.readchar

function e.cancel(b)

	-- do nothing. cancel selection if any
	b.si, b.sj = nil, nil
	b.chgd = true
end 

e.redisplay = editor.fullredisplay

function e.subshell()
	clear()
	repl()
	e.redisplay()
end

function e.gohome(b) b:setcurj(0) end
function e.goend(b) b:setcurj() end
function e.gobot(b) b:setcur(1, 0) end
function e.goeot(b) b:setcur() end
	
function e.goup(b, n) 
	n = n or 1
	b:setcur(b.ci - n, b.cj)
end

function e.godown(b, n)
	n = n or 1
	b:setcur(b.ci + n, b.cj)
end

function e.goright(b, n)
	n = n or 1
	local ln = #b.ll[b.ci]
	while n > 0 do
		b.cj = b.cj + 1
		if b.cj > ln then -- goto beg of next line
			b.ci = b.ci + 1
			if b.ci > #b.ll then
				e.goeot(b)
				return false 
			end
			ln = #b.ll[b.ci]
			b.cj = 0
		end
		n = n - 1
	end
	return true
end

function e.goleft(b, n)
	n = n or 1
	while n > 0 do
		b.cj = b.cj - 1
		if b.cj < 0 then -- goto end of prev line
			b.ci = b.ci - 1
			if b.ci < 1 then 
				e.gobot(b)
				return false
			end
			b.cj = #b.ll[b.ci]
		end
		n = n - 1
	end
	return true
end

function e.nextword(b)
	local l = e.getline(b)
	local ci, cj, ln = e.getcur(b)
	if cj >= #l and ci < ln then -- at eol and not at eot
		e.godown(b); e.gohome(b)
		return
	end
	local j = l:find("[%s%p][%w]", cj+1)
	if j then 
		e.setcur(b, ci, j) --before first word char
		return true
	end
	e.goend(b)
end--nextword

function e.prevword(b)
	local l = e.getline(b)
	local ci, cj, ln = e.getcur(b)
	local j = cj - 1
	while true do
		if j == 0 then 
			e.gohome(b)
			return
		elseif j <= 0 then
			if ci > 1 then 
				e.goup(b); e.goend(b)
			else
				e.gohome(b)
			end
			return
		end
		if l:match("^[%s%p][%w]", j) then
			e.setcur(b, ci, j) --before first word char
			return true
		end
		j = j - 1
	end
end--prevword

function e.pgdn(b) 
	b:setcur(b.ci + b.box.l - 2, b.cj)
end

function e.pgup(b) 
	b:setcur(b.ci - b.box.l - 2, b.cj)
end

function e.hpgdn(b) 
	b:setcur(b.ci + b.box.l//2, b.cj)
end

function e.hpgup(b) 
	b:setcur(b.ci - b.box.l//2, b.cj)
end

function e.del(b)
	-- if selection, delete it. Else, delete char
	if b.si then 
		return e.wipe(b, true) -- do not keep in wipe list
	end
	if b:ateot() then return false end
	local ci, cj = b:getcur()
	if b:ateol() then return b:bufdel(ci+1, 0) end
	return b:bufdel(ci, cj+1)
end

function e.bksp(b)
	return e.goleft(b) and e.del(b)
end

function e.insch(b, k)
	-- insert char with code k
	-- (don't remove. used by editor loop)
	return b:bufins(char(k))
end

function e.tab(b)
	local tn = editor.tabspaces
	if not tn then -- insert a tab char
		return b:bufins(char(9)) 
	end
	local ci, cj = b:getcur()
	local n = tn - ((cj) % tn)
	return b:bufins(string.rep(char(32), n))
end
	

function e.insert(b, x)
	-- insert x at cursor
	-- x can be a string or a list of lines (a table)
	-- if x is a string, it may contain newlines ('\n')
	return b:bufins((type(x) == "string") and lines(x) or x)
end

function e.nl(b)
	-- equivalent to e.insert("\n")
	return b:bufins({"", ""})
end

function e.searchpattern(b, pat, plain)
	-- forward search a lua or plain text pattern pat, starting at cursor.
	-- pattern is searched one line at a time
	-- if plain is true, pat is a plain text pattern. special
	-- pattern chars are ignored.
	-- in a lua pattern, ^ and $ represent the beginning and end of line
	-- a pattern cannot contain '\n'
	-- if the pattern is found, cursor is moved at the beginning of
	-- the pattern and the function return true. else, the cursor
	-- is not moved and the function returns false.

	local oci, ocj = b:getcur() -- save the original cursor position
	while true do
		local l, cj = b:getline()
		local j = l:find(pat, cj+2, plain)
		if j then --found
			b:setcurj(j-1)
			return true
		end -- found
		if b:atlast() then break end
		e.gohome(b); e.godown(b, 1)
	end--while
	-- not found
	b:setcur(oci, ocj) -- restore cursor position	
	return false
end

function e.searchagain(b, actfn)
	-- search editor.pat. If found, execute actfn
	-- default action is to display a message "found!")
	-- on success, return the result of actfn() or true.
	-- (note: search does NOT ignore case)

	if not editor.pat then 
		msg("no string to search")
		return nil
	end
	local r = e.searchpattern(b, editor.pat, editor.searchplain)
	if r then
		if actfn then 
			return actfn()
		else
			msg("found!")
			return true
		end
	else
		msg("not found")
	end
end

function e.search(b)
	editor.pat = readstr("Search: ")
	if not editor.pat then 
		msg("aborted.")
		return
	end
	return e.searchagain(b)
end

function e.replaceagain(b)

	local replall = false -- true if user selected "replace (a)ll"
	local n = 0 -- number of replaced instances
	function replatcur()
		-- replace at cursor 
		-- (called only when editor.pat is found at cursor)
		-- (pat and patrepl are plain text, unescaped)
		n = n + 1  --one more replaced instance
		local ci, cj = b:getcur()
		return b:bufdel(ci, cj + #editor.pat) 
			and b:bufins(editor.patrepl)
	end--replatcur
	function replfn()
		-- this function is called each time editor.pat is found
		-- return true to continue, nil/false to stop
		if replall then 
			return replatcur()
		else
			local ch = readchar( -- ask what to do
				"replace? (q)uit (y)es (n)o (a)ll (^G) ", 
				"[anqy]")
			if not ch then return nil end
			if ch == "a" then -- replace all
				replall = true
				return replatcur()
			elseif ch == "y" then -- replace
				return replatcur()
			elseif ch == "n" then -- continue
				return true
			else -- assume q (quit)
				return nil
			end
		end
	end--replfn
	while e.searchagain(b, replfn) do end
	msg(strf("replaced %d instance(s)", n))
end--replaceagain

function e.replace(b)
	editor.pat = readstr("Search: ")
	if not editor.pat then 
		msg("aborted.")
		return
	end
	editor.patrepl = readstr("Replace with: ")
	if not editor.patrepl then 
		msg("aborted.")
		return
	end
	return e.replaceagain(b)
end--replace

function e.mark(b)
	b.si, b.sj = b.ci, b.cj
	msg("Mark set.")
	b.chgd = true
end

function e.exch_mark(b)
	if b.si then
		b.si, b.ci = b.ci, b.si
		b.sj, b.cj = b.cj, b.sj
	end
end

function e.killeol(b)
  -- kill to end of current line
  local xi, xj = b.ci, #b.ll[b.ci]
  editor.kll = b:getlines(xi, xj)
  b:bufdel(xi, xj)
  return 
end--killeol

function e.copy(b, nokeep)
	-- copy selection
	-- make sure cursor is at beg of selection
	if b:markbeforecur() then e.exch_mark(b) end 
	local si, sj = b:getsel()
	editor.kll = b:getlines(si, sj) 
	b.si = nil
end--copy	

function e.wipe(b, nokeep)
	-- wipe selection, or kill current line if no selection
	-- if nokeep is true, deleted text is not kept in the kill list
	-- (default false)
	if not b.si then 
		msg("No selection.")
		if b:atlast() then -- add an empty line at end
			e.goeot(b); e.nl(b); e.goup(b)
		end
		e.gohome(b)
		local xi, xj
		xi, xj = b.ci+1, 0
		if not nokeep then editor.kll = b:getlines(xi, xj) end
		b:bufdel(xi, xj)
		return 
	end
	-- make sure cursor is at beg of selection
	if b:markbeforecur() then e.exch_mark(b) end 
	local si, sj = b:getsel()
	if not nokeep then editor.kll = b:getlines(si, sj) end
	b:bufdel(si, sj)
	b.si = nil
end--wipe	

function e.yank(b)
	if not editor.kll or #editor.kll == 0 then 
		msg("nothing to yank!"); return 
	end
	return b:bufins(editor.kll)
end--yank

function e.undo(b)
	if b.ualtop == 0 then msg("nothing to undo!"); return end
	b:op_undo(b.ual[b.ualtop])
	b.ualtop = b.ualtop - 1
end--undo

function e.redo(b)
	if b.ualtop == #b.ual then msg("nothing to redo!"); return end
	b.ualtop = b.ualtop + 1
	b:op_redo(b.ual[b.ualtop])
end--redo

function e.exiteditor(b)
	local anyunsaved = false
	for i, bx in ipairs(editor.buflist) do 
		-- tmp buffers: if name starts with '*', 
		-- buffer is not considered as unsaved
		if bx.filename and not bx.filename:match("^%*") then
			anyunsaved = anyunsaved or bx.unsaved
		end
	end
	if anyunsaved then
		local ch = readchar(
			"Some buffers not saved. Quit? ", "[YNQynq\r\n]")
		if ch ~= "y" and ch ~= "Y" then 
			msg("aborted.")
			return
		end
	end
	editor.quit = true
	msg("exiting.")
end


function e.newbuffer(b, fname, ll)
	ll = ll or { "" } -- default is a buffer with one empty line
	fname = fname or editor.readstr("Buffer name: ")
	-- try to find the buffer if it already exists
	for i, bx in ipairs(editor.buflist) do
		if bx.filename == fname then
			editor.buf = bx; editor.bufindex = i
			editor.fullredisplay(bx)
			return bx
		end
	end
	-- buffer doesn't exist. create it.
	local bx = buffer.new(ll) 
	bx.actions = editor.edit_actions 
	bx.filename = fname
	-- insert just after the current buffer
	local bi = editor.bufindex + 1 
	table.insert(editor.buflist, bi, bx) 
	editor.bufindex = bi
	editor.buf = bx
	editor.fullredisplay()
	return bx
end

function e.nextbuffer(b)
	-- switch to next buffer
	local bln = #editor.buflist
	editor.bufindex = editor.bufindex % bln + 1
	editor.buf = editor.buflist[editor.bufindex]
	editor.fullredisplay()
end--nextbuffer

function e.prevbuffer(b)
	-- switch to previous buffer
	local bln = #editor.buflist
	-- if bufindex>1, the "previous" buffer index should be bufindex-1
	-- if bufindex==1, the "previous" buffer index should be bln
	editor.bufindex = (editor.bufindex - 2) % bln + 1
	editor.buf = editor.buflist[editor.bufindex]
	editor.fullredisplay()
end--nextbuffer

function e.outbuffer(b)
	-- switch to *OUT* buffer. 
	-- if already in OUT buffer, switch back to previous buffer
	if b.filename == "*OUT*" then return e.prevbuffer(b) end
	return e.newbuffer(b, "*OUT*")
end --outbuffer
	
function e.findfile(b, fname)
	fname = fname or editor.readstr("Open file: ")
	if not fname then editor.msg""; return end
	local ll, errmsg = readfile(fname)
	if not ll then editor.msg(errmsg); return end
	e.newbuffer(b, fname, ll)
end--findfile

function e.writefile(b, fname)
	fname = fname or editor.readstr("Write to file: ")
	if not fname then editor.msg("Aborted."); return end
	fh, errmsg = io.open(fname, "w")
	if not fh then editor.msg(errmsg); return end
	for i = 1, #b.ll do fh:write(b.ll[i], "\n") end
	fh:close()
	b.filename = fname
	b.unsaved = false
	editor.msg(fname .. " saved.")
end--writefile

function e.savefile(b)
	e.writefile(b, b.filename)
end--savefile

function e.gotoline(b, lineno)
	-- prompt for a line number, go there
	-- if lineno is provided, don't prompt.
	lineno = lineno or tonumber(editor.readstr("line number: "))
	if not lineno then
		msg("invalid line number.")
	else
		return b:setcur(lineno, 0)
	end
end--gotoline

function e.help(b)
	for i, bx in ipairs(editor.buflist) do
		if bx.filename == "*HELP*" then
			editor.buf = bx; editor.bufindex = i
			editor.fullredisplay(bx)
			return
		end
	end -- help buffer not found, then build it.
	return e.newbuffer(b, "*HELP*", lines(editor.helptext))
end--help

function e.prefix_ctlx(b)
	-- process ^X prefix
	local k = editor.nextk()
	local kname = "^X-" .. editor.keyname(k)
	local act = editor.bindings_ctlx[k]
	if not act then
		msg(kname .. " not bound.")
		return false
	end
	msg(kname)
	return act(b)	
end--prefix_ctlx

function e.prefix_esc(b)
	-- process ESC prefix
	local k = editor.nextk()
	local kname = "ESC-" .. editor.keyname(k)
	local act = editor.bindings_esc[k]
	if not act then
		msg(kname .. " not bound.")
		return false
	end
	msg(kname)
	return act(b)	
end--prefix_esc

-- useful functions for extensions (see usage examples in ple_init.lua)

function e.getcur(b)
	-- return the cursor position in the buffer b and the number
	-- of lines in the buffer
	-- eg.:  ci, cj, ln = e.getcur(b) 
	-- ci is the line index and cj the column index
	-- ci is in the range [1, ln]
	-- cj is in the range [0, line length]
	-- cj == 0 when the cursor is at the beginning of the line
	--	(ie. before the first char)
	return b.ci, b.cj, #b.ll
end

function e.setcur(b, ci, cj)
	-- move the cursor to position ci, cj (see above)
	b:setcur(ci, cj)
end

function e.getline(b, i)
	-- return the i-th line of the buffer b (as a string)
	-- if i is not provided, the current line is returned.
	i = i or b.ci
	return b.ll[i]
end

function e.test(b)
	-- this function is just used for quick debug tests
	-- (to be removed!)
	--
--~ 	s = b:gettext()
--~ 	s = s:upper()
--~ 	b:settext(s)

	-- test readchar
	while true do
		local ch = readchar("test readchar (space to quit): ", ".")
		if not ch or ch == " " then msg("aborted!"); break
		else 
			e.goeot(b); e.nl(b);
			e.insert(b, strf("readchar => %d", byte(ch)))
		end
	end
end--atest

editor.helptext = [[

*HELP*			-- back to the previous buffer: ^X^P

Cursor movement
	Arrows, PageUp, PageDown, Home, End
	Ctl-Arrows	word left/right an up/down half screen
	^A, ^E		go to beginning, end of line
	^B, ^F		go backward, forward
	^N, ^P		go to next line, previous line
	^U, ^V		page up, page down
	ESC <		go to beginning of buffer
	ESC > 		go to end of buffer
	^S		forward search (plain text, case sensitive)
	^R		search again (string previously entered with ^S)
	^X^G		prompt for a line number, go there
   
Edition
	^D, Delete	delete character at cursor
	^H, bcksp	delete previous character
	^space		mark  (set beginning of selection)
	^W		wipe (cut selection or cut line if no selection)
	ESC s		open a sub-repl (return with ^D on empty line)
	ESC w		copy selection
	^Y		yank (paste)
	ESC 5		replace
	ESC 7		replace again (with same strings)

Files, buffers
	^X^F		prompt for a filename, read the file in a new buffer
	^X^W		prompt for a filename, write the current buffer
	^X^S		save the current buffer
	^X^B		switch to a named buffer or create a new buffer
	^X^N		switch to the next buffer
	^X^P		switch to the previous buffer

Misc.
	^X^C		exit the editor
	^G		abort the current command
	^Z		undo 
	^X^Z		redo 
	^L		redisplay the screen (useful if the screen was 
			garbled	or its dimensions changed)
	F1, ^X^H	this help text

]]

------------------------------------------------------------------------
-- bindings

editor.bindings = { -- actions binding for text edition
	[1] = e.gohome,		-- ^A
	[2] = e.goleft,		-- ^B
	--[3]		-- ^C
	[4] = e.del,		-- ^D
	[5] = e.goend,		-- ^E
	[6] = e.goright,	-- ^F
	[7] = e.cancel,		-- ^G 
	[8] = e.bksp,		-- ^H
	[9] = e.tab,		-- ^I
	[10] = e.nl,	-- ^J
	[11] = e.killeol,	-- ^K
	[12] = e.redisplay,	-- ^L
	[13] = e.nl,		-- ^M (return)
	[14] = e.godown,	-- ^N
	[15] = e.outbuffer,  	-- ^O
	[16] = e.goup,		-- ^P
	--[17] = e.mark,	-- ^Q
	[18] = e.searchagain,	-- ^R
	[19] = e.search,	-- ^S
	[20] = e.test,		-- ^T
	[21] = e.pgup,		-- ^U
	[22] = e.pgdn,		-- ^V
	[23] = e.wipe,		-- ^W
	[24] = e.prefix_ctlx,	-- ^X (prefix - see below)
	[25] = e.yank,		-- ^Y
	[26] = e.undo,		-- ^Z
	[27] = e.prefix_esc,	-- ESC (prefix - see below)
	--
	[128] = e.mark,
	--
	[keys.kpgup]  = e.pgup,
	[keys.kpgdn]  = e.pgdn,
	[keys.khome]  = e.gohome,
	[keys.kend]   = e.goend,
	[keys.kdel]   = e.del, 
	[keys.del]    = e.bksp, 
	[keys.kright] = e.goright,
	[keys.kleft]  = e.goleft,
	[keys.kctlright] = e.nextword,
	[keys.kctlleft]  = e.prevword,
	[keys.kup]    = e.goup,
	[keys.kdown]  = e.godown,
	[keys.kctlup] = e.hpgup,
	[keys.kctldown]  = e.hpgdn,
	[keys.kf1]    = e.help,
}--editor.bindings

editor.bindings_ctlx = {  -- ^X<key>
	[2] = e.newbuffer,	-- ^X^B
	[3] = e.exiteditor,	-- ^X^C
	[6] = e.findfile,	-- ^X^F
	[7] = e.gotoline,	-- ^X^G
	[8] = e.help,		-- ^X^H
	[11] = e.killeol,	-- ^X^K
	[14] = e.nextbuffer,	-- ^X^N
	[16] = e.prevbuffer,	-- ^X^P
	[19] = e.savefile,	-- ^X^S
	[23] = e.writefile,	-- ^X^W
	[24] = e.exch_mark,	-- ^X^X		
	[26] = e.redo,		-- ^X^Z
	[53] = e.replace,	-- ^X 5 -%
	[55] = e.replaceagain,	-- ^X 7 -&
}--editor.bindings_ctlx
	
editor.bindings_esc = {  -- ESC<key>
	[53] = e.replace,	-- ESC 5 -%
	[55] = e.replaceagain,	-- ESC 7 -&
	[60] = e.gobot,		-- ESC <
	[62] = e.goeot,		-- ESC >	
	[115] = e.subshell,	-- ESC s
	[119] = e.copy,		-- ESC w
}--editor.bindings_esc
	
local function editor_loadinitfile()
	-- function to be executed before entering the editor loop
	-- could be used to load a configuration/initialization file
	local initfile = os.getenv("PLE_INIT")
	if fileexists(initfile) then 
		return assert(loadfile(initfile))()
	end
	initfile = "./ple_init.lua"
	if fileexists(initfile) then
		return assert(loadfile(initfile))()
	end
	local homedir = os.getenv("HOME") or "~"
	initfile = homedir .. "/.config/ple/ple_init.lua"
	if fileexists(initfile) then
		return assert(loadfile(initfile))()
	end
	return nil
end--editor_loadinitfile


local function editor_loop(ll, fname)
  editor.init()
	editor.initmsg = "Help: F1 or ^X^H"
	local r = nil -- editor_loadinitfile()
	style.normal()
	e.newbuffer(nil, fname, ll); 
		-- 1st arg is current buffer (unused for newbuffer, so nil) 
	msg(editor.initmsg)
	redisplay(editor.buf) -- adjust cursor to beginning of buffer
  	editor.quit = nil
	while not editor.quit do
		local k = editor.nextk()
		local kname = editor.keyname(k)
		-- try to find an action bound to the key
		-- first in buffer action table
		-- then in the default editor table
		local act = editor.buf.bindings
			and editor.buf.bindings[k]
			or editor.bindings[k] 
		if act then 
			msg(kname)
			editor.lastresult = act(editor.buf)
		elseif (not k2) and ((k >= 32 and k < 127) 
			or (k >= 160 and k < 256) 
			or (k == 9)) then
			editor.lastresult = e.insch(editor.buf, k)
		else
			editor.msg(kname .. " not bound")
		end
	redisplay(editor.buf)
	end--while true
end--editor_loop

function edit(fname)
	-- process argument
  local fnane = fname
	local ll
	if fname then
		ll, err = readfile(fname) -- load file as a list of lines
		--if not ll then print(err); return end
		if not ll then 
			ll = { "" }
		end
	else
		ll = { "" }
		fname = "unnamed"
	end
	-- set term in raw mode
	local prevmode, e, m = term.savemode()
	if not prevmode then print(prevmode, e, m); return end
	term.setrawmode()
	term.reset()
	-- run the application in a protected call so we can properly reset
	-- the tty mode and display a traceback in case of error
	local ok, msg = xpcall(editor_loop, debug.traceback, ll, fname)
	-- restore terminal in a a clean state
	term.show() -- show cursor
	--term.left(999); term.down(999)
	style.normal()
	flush()
	--con.setmode(omode)
	term.restoremode(prevmode)	
  if not ok then -- display traceback in case of error
		print(msg)
		return
  else 
    term.reset()
	end 
	return "" -- to preven "nil" from repl
end
return"ok"
