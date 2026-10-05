file(GLOB_RECURSE GAME_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

add_library(GameModule SHARED ${GAME_SOURCES})

target_compile_definitions(GameModule PRIVATE BUILD_GAME)

target_include_directories(GameModule PRIVATE
  ${CMAKE_SOURCE_DIR}/src/
  ${CMAKE_SOURCE_DIR}/submodules/json/single_include/
  ${CMAKE_SOURCE_DIR}/submodules/jolt/
  ${CMAKE_SOURCE_DIR}/submodules/
  ${CMAKE_SOURCE_DIR}/submodules/glm
  ${CMAKE_SOURCE_DIR}/submodules/freetype/include/
  ${FMOD_API_DIR}/inc
)

target_link_libraries(GameModule PRIVATE Engine)