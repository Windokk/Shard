file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

# Module "game", built like any plugin. Written against the SDK alone (ABI c1), so what builds here builds for a game outside the repository.
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
