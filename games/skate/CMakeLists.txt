# skate (working title): a skateboarding game for one to four players in split screen (DESIGN.md).
#   skate         the game
#   skate_auto    bots skate it (for hosts that send no input, and the gates)
#   skate_bench   four bots in a four-way split on the mega ramp park (the perf gate's scene; carts read no environment)
#   skate_check   the simulation's checks with no window and no GPU (native only): ramp physics, the parks (nothing
#                 floats, every line skateable) and the soak
set(SIM src/park.c src/parks.c src/skater.c src/sim.c src/bot.c src/nav.c)
set(SRC src/main.c src/view.c src/scenery.c src/pose.c src/rag.c src/hud.c src/gfx.c src/audio.c ${SIM})
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c)
  set(CW_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/skate.txt)
  cw_add_app(skate SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
  cw_add_app(skate_auto SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
  target_compile_definitions(skate_auto PRIVATE SKATE_AUTOPILOT=1)
  add_dependencies(skate_auto_pack skate_pack)
  cw_add_app(skate_bench SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
  target_compile_definitions(skate_bench PRIVATE SKATE_AUTOPILOT=1 SKATE_BENCH=1)
  add_dependencies(skate_bench_pack skate_pack)
endif()
if(NOT CW_CART)
  add_executable(skate_check src/check.c ${SIM})
  target_link_libraries(skate_check PRIVATE cartwheel)
endif()
