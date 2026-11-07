# adventure (working title): a third-person action adventure in one connected world (DESIGN.md).
#   adventure        the game
#   adventure_auto   a bot plays it through the controls (for hosts that send no input, the gates and the soak)
#   adventure_bench  the bot in the busiest fight with the most drawn (the perf gate's scene; carts read no environment)
set(SRC main.c sim.c world.c view.c bot.c fx.c)
set(CW_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/adventure.txt)
cw_add_app(adventure SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
cw_add_app(adventure_auto SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(adventure_auto PRIVATE ADV_AUTOPILOT=1)
add_dependencies(adventure_auto_pack adventure_pack)
cw_add_app(adventure_bench SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(adventure_bench PRIVATE ADV_AUTOPILOT=1 ADV_BENCH=1)
add_dependencies(adventure_bench_pack adventure_pack)
