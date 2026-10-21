# scratch/swarm-air/CMakeLists.txt
# Builds the swarm radio: UAVLnQ's Wi-Fi mesh as the link between the drone containers.

get_filename_component(absdir ${CMAKE_CURRENT_SOURCE_DIR} ABSOLUTE)
string(REPLACE "${PROJECT_SOURCE_DIR}" "${CMAKE_OUTPUT_DIRECTORY}" scratch_directory ${absdir})

build_exec(
  EXECNAME swarm_air
  EXECNAME_PREFIX scratch_
  SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/main.cc
  LIBRARIES_TO_LINK "${ns3-libs}" "${ns3-contrib-libs}"
  EXECUTABLE_DIRECTORY_PATH ${scratch_directory}/
)
