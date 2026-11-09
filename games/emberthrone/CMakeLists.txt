# Ember Throne: a third-person action adventure in one connected world (DESIGN.md).
#   emberthrone        the game
#   emberthrone_auto   a bot plays it through the controls (for hosts that send no input, the gates and the soak)
#   emberthrone_bench  the bot in the busiest fight with the most drawn (the perf gate's scene; carts read no environment)
set(SRC main.c sim.c world.c view.c bot.c fx.c rules.c ${CMAKE_CURRENT_SOURCE_DIR}/../common/coplanar.c)
set(CW_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/emberthrone.txt)
cw_add_app(emberthrone SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
cw_add_app(emberthrone_auto SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(emberthrone_auto PRIVATE ADV_AUTOPILOT=1)
add_dependencies(emberthrone_auto_pack emberthrone_pack)
cw_add_app(emberthrone_bench SOURCES ${SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(emberthrone_bench PRIVATE ADV_AUTOPILOT=1 ADV_BENCH=1)
add_dependencies(emberthrone_bench_pack emberthrone_pack)
