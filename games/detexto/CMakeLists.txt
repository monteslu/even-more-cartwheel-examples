# Detexto: Destructo's game drawn as neon text. A copy of games/destructo (its simulation, levels and controls)
# with every frame turned into glowing characters on black by the ASCII post pass (src/ascii.c); the HUD and menus
# stay clear 2D on top.
#   detexto          the game
#   detexto_auto     the bot plays it through the controls (for hosts that send no input)
#   detexto_bench    the perf scene: the bot on Taj Mahal, the collapse in view
set(DT_SIM src/loader.c src/sim.c src/chunks.c src/levels.c src/landmarks.c src/lm_data.c src/bot.c src/replay.c src/opts.c src/water.c)
set(DT_SRC src/main.c src/view.c src/ascii.c src/dither.c src/trees.c src/fx.c src/sound.c ${CMAKE_CURRENT_SOURCE_DIR}/../common/coplanar.c ${DT_SIM})
set(CW_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/programs/detexto.txt)
cw_add_app(detexto SOURCES ${DT_SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
cw_add_app(detexto_auto SOURCES ${DT_SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(detexto_auto PRIVATE DETEXTO_AUTOPILOT=1)
add_dependencies(detexto_auto_pack detexto_pack)
cw_add_app(detexto_bench SOURCES ${DT_SRC} PACK ${CMAKE_CURRENT_SOURCE_DIR}/assets/roots.txt)
target_compile_definitions(detexto_bench PRIVATE DETEXTO_AUTOPILOT=1 DETEXTO_BENCH=1)
add_dependencies(detexto_bench_pack detexto_pack)
