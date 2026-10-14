#include "lua.h"
#include "luacode.h"
#include "lualib.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <print>
#include <string>

int main(int argc, char** argv) {
  if (argc != 2)
    return 1;
  std::ifstream file(argv[1]);
  if (!file)
    return 1;
  const std::string module{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  const std::string source = "local M = (function()\n" + module + R"(
end)()
local e = M.new()
local function silent()
  for _, series in ipairs({e.far, e.near}) do
    for _, value in ipairs(series) do assert(value == 0, 'stale audio remained visible') end
  end
end
local signal = {}
for i = 1, 24 do signal[i] = i == 12 and '0.9' or '0.1' end
-- Populate a quiet history, then observe a real loud pulse moving left.
local count = #e.far
for frame = 1, count * 2 do
  local now = 1000 + frame * 80
  e:accept(string.rep('0.1,', 23) .. '0.1', now, true)
  e:step(now, true)
end
local quiet = e.far[math.floor(count / 2)]
assert(quiet > 0 and quiet < 0.2, 'quiet audio gained an invented loud ridge')
local now = 1000 + (count * 2 + 1) * 80
e:accept(string.rep('0.9,', 23) .. '0.9', now, true)
e:step(now, true)
e:accept(string.rep('0.1,', 23) .. '0.1', now + 80, true)
e:step(now + 80, true)
e:accept(string.rep('0.1,', 23) .. '0.1', now + 160, true)
e:step(now + 160, true)
assert(e.far[count - 2] > quiet, 'real loud pulse did not appear in the recent history')
e:accept(string.rep('0.1,', 23) .. '0.1', now + 240, true)
e:step(now + 240, true)
assert(e.far[count - 3] > quiet, 'real pulse did not move left with time')
assert(e.far[1] == 0 and e.far[count] == 0, 'mist touched the wooden edges')
for i, value in ipairs(e.far) do
  assert(value >= 0 and value <= 1, 'envelope escaped graph bounds')
  assert(e.near[i] >= 0 and e.near[i] <= value, 'foreground crossed the rear layer')
end
e:step(1100, false)
silent()
e:accept(table.concat(signal, ','), 1200, true)
e:step(1250, true)
e:step(1600, true)
silent()
e:accept(table.concat(signal, ','), 1700, true)
e:step(1750, true)
assert(not e:accept('', 1760, true))
silent()
assert(not e:accept('0.8,broken', 1800, true))
silent()
signal[12] = 'nan'
assert(not e:accept(table.concat(signal, ','), 1900, true))
silent()
signal[12] = '99'
signal[1] = '-4'
assert(e:accept(table.concat(signal, ','), 2000, true))
e:step(2050, true)
for _, value in ipairs(e.far) do assert(value >= 0 and value <= 1) end
e:accept(table.concat(signal, ','), 2100, false)
silent()
)";
  lua_State* state = luaL_newstate();
  luaL_openlibs(state);
  std::size_t size = 0;
  char* bytecode = luau_compile(source.data(), source.size(), nullptr, &size);
  int result = bytecode == nullptr ? 1 : luau_load(state, "=music-spectrum", bytecode, size, 0);
  std::free(bytecode);
  if (result == 0)
    result = lua_pcall(state, 0, 0, 0);
  if (result != 0)
    std::println(stderr, "music preview: {}", lua_tostring(state, -1));
  lua_close(state);
  return result == 0 ? 0 : 1;
}
