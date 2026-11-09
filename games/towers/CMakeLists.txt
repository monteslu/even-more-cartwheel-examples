# towers (working title): a tower defense game for one to four players on one machine (DESIGN.md).
#   towers         the game
#   towers_auto    bots play it (for hosts that send no input)
#   towers_bench   a late map held by towers on every slot against 2400 creeps on its paths (the perf gate's scene;
#                  carts read no environment)
#   towers_soak    the rules and the bots with no window and no GPU (native only): the long soak and the map check's
#                  clear run
#   towers_mapview builds a map as the game does: review in romdev, and the native bakes (tools/mapview)
set(SRC main.c sim.c bot.c view.c fx.c map.c mapbuild.c)
set(CW_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/towers.txt)
cw_add_app(towers SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
cw_add_app(towers_auto SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(towers_auto PRIVATE TOWERS_AUTOPILOT=1)
add_dependencies(towers_auto_pack towers_pack)
cw_add_app(towers_bench SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(towers_bench PRIVATE TOWERS_AUTOPILOT=1 TOWERS_BENCH_MAP=26 TOWERS_BENCH_CROWD=2400 TOWERS_AUTO_PLAYERS=4)
add_dependencies(towers_bench_pack towers_pack)
foreach(t towers towers_auto towers_bench)
  target_compile_definitions(${t} PRIVATE TOWERS_RENDER=1)
endforeach()
if(NOT CW_CART)
  add_executable(towers_soak soak.c sim.c bot.c newcomer.c map.c)
  target_link_libraries(towers_soak PRIVATE cartwheel)
  target_compile_definitions(towers_soak PRIVATE TOWERS_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
endif()
include(tools/mapview/mapview.cmake)
