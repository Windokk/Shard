file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

# The game is the module "game" : built like any other plugin, laid out as <build>/plugins/game/{shard-plugin.json,bin/<platform>/}.
# Pass the library to the executables with --game ($<TARGET_FILE:GameModule>), or --plugin <build>/plugins/game to use the manifest.
#
# It is written against the SDK alone (ABI c1) : it includes <shard/game.hpp> and nothing of the engine, and links nothing of it, so what
# builds here is what a game outside this repository builds.
shard_add_plugin(
  NAME         GameModule
  ID           game
  VERSION      ${PROJECT_VERSION}
  KIND         module
  MODULE_KIND  code
  HOSTS        engine tool
  ABI          c1
  DESCRIPTION  "The game's own components and scripts"
  SOURCES      ${GAME_SOURCES}
)
set_target_properties(GameModule PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
