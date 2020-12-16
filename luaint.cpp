//
/// luaint.cpp
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                
// GNU General Public License for more details.                                 
//                                                                              
// You should have received a copy of the GNU General Public License            
// along with this program.  If not, see <http://www.gnu.org/licenses/>.        
//        

#include <lua.hpp>
#include <circle/startup.h>
                                                                      
#include "kernel.h"

#include "lconsole.h"
#include "lfilesystem.h"
#include "luaint.h"


extern "C"
void lupos_yield() {
  CKernel::Get()->mScheduler.Yield();
}

extern "C"
void print_error(lua_State* state) {
  // The error message is on top of the stack.                                  
  // Fetch it, print it and then pop it off the stack.                          
  const char* message = lua_tostring(state, -1);
  puts(message);
  lua_pop(state, 1);
}

extern "C"
int lua_reboot(lua_State* state) {
  reboot();
  return 0;
}

extern "C"
int lua_usleep(lua_State* state) {
  lua_Integer arg = luaL_checkinteger(state, 1);
  CTimer::Get()->usDelay(arg);
  return 0;
}

extern "C"
int lua_interface(CKernel *mKernel) {
  lua_State *state = luaL_newstate();
  luaL_openlibs(state);
  luaopen_console(state);
  lua_setglobal(state, "console");
  luaopen_filesystem(state);
  lua_setglobal(state, "filesystem");
  lua_register(state, "reboot", lua_reboot);
  lua_register(state, "usleep", lua_usleep);
  int result = luaL_loadfile(state, "hello.lua");
  if ( result != LUA_OK && result != LUA_ERRFILE) {
    print_error(state);
    return -1;
  }
  if(result == LUA_OK) {
    result = lua_pcall(state, 0, LUA_MULTRET, 0);
    if ( result != LUA_OK ) {
      print_error(state);
    }
  }

  char line[200];                                                         
  while(1) {                                                              
    //mUSBHCI.UpdatePlugAndPlay();                                          
    printf("L0>");                                                        
    fflush(stdout);                                                       
    if (fgets(line, sizeof(line), stdin) != nullptr) {
      // trim line feed
      line[strlen(line)-1] = '\0';
      // check if bare fn name given
      lua_getglobal(state, line);
      int objtype = lua_type(state, -1);
      if( objtype == LUA_TFUNCTION ) {
	// try with no args
	strncat(line, "()", sizeof(line)-strlen(line)-1);
      }
      result = luaL_loadstring(state, line);                              
      if ( result != LUA_OK ) {                                           
	print_error(state);                                               
      } else {                                                            
	result = lua_pcall(state, 0, LUA_MULTRET, 0);                     
	if ( result != LUA_OK ) {                                         
	  print_error(state);                                             
	}                                                                 
      }                                                                   
    }                                                                     
  }                                                                       
                                                                                
  lua_close(state);

  return 0;
}
