
file(GLOB_RECURSE VTFLibSources "ext/vtfedit_reloaded/VTFLib/*.cpp")
message("${VTFLibSources}")
add_library(VTFLib20 SHARED  ${VTFLibSources})