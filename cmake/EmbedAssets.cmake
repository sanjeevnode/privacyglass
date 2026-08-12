# Turns web/*.js|css into a C++ header of raw string literals, so the injected
# script is compiled into the exe (it runs before page JS, so it cannot fetch files).
# Re-runs whenever a listed asset changes.

function(embed_web_assets OUT_HEADER)
  set(assets ${ARGN})
  set(deps "")
  foreach(a ${assets})
    list(APPEND deps "${CMAKE_SOURCE_DIR}/${a}")
  endforeach()

  add_custom_command(
    OUTPUT ${OUT_HEADER}
    COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DOUT_HEADER=${OUT_HEADER}
            "-DASSETS=${assets}"
            -P ${CMAKE_SOURCE_DIR}/cmake/GenerateAssets.cmake
    DEPENDS ${deps} ${CMAKE_SOURCE_DIR}/cmake/GenerateAssets.cmake
    COMMENT "Embedding web assets"
    VERBATIM)
endfunction()
