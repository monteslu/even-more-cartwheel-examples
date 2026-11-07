# towers_mapview: a map as the game builds it, for review (a cart in romdev) and for the native bakes of every map's
# lightmap (--bake <dir>); see mapview.c. mapbuild.c is the builder the game links too.
cw_add_app(towers_mapview
  SOURCES ${CMAKE_CURRENT_LIST_DIR}/mapview.c ${CMAKE_CURRENT_LIST_DIR}/../../mapbuild.c ${CMAKE_CURRENT_LIST_DIR}/../../map.c ${CMAKE_CURRENT_LIST_DIR}/../../sim.c
  PACK ${CMAKE_CURRENT_LIST_DIR}/../../assets/roots.txt
  PROGRAMS ${CMAKE_CURRENT_LIST_DIR}/programs/towers_mapview.txt)
target_include_directories(towers_mapview PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../..)
add_dependencies(towers_mapview_pack towers_pack)
