file(GLOB_RECURSE GAME_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

# The game is the module "game" : built like any other plugin, laid out as <build>/plugins/game/{shard-plugin.json,bin/<platform>/}.
# Pass the library to the executables with --game ($<TARGET_FILE:GameModule>), or --plugin <build>/plugins/game to use the manifest.
shard_add_plugin(
  NAME         GameModule
  ID           game
  VERSION      ${PROJECT_VERSION}
  KIND         module
  MODULE_KIND  code
  HOSTS        engine tool
  DESCRIPTION  "The game's own components and scripts"
  SOURCES      ${GAME_SOURCES}
)
