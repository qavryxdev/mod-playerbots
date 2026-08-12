#
# Optional per-module CMake hook, included by modules/CMakeLists.txt.
#
# Registers the module's unit tests with the core's unit_tests target. The tests deliberately live
# outside src/, because everything under src/ is swept into the modules library by CollectSourceFiles
# and a gtest translation unit must not end up linked into worldserver.
#
# Build and run:
#   cmake .. -DBUILD_TESTING=ON
#   make -j$(nproc) unit_tests
#   ctest -j$(nproc) --output-on-failure
#

if(BUILD_TESTING)
  set(MOD_PLAYERBOTS_TEST_DIR "${CMAKE_CURRENT_LIST_DIR}/tests")

  file(GLOB MOD_PLAYERBOTS_TEST_SOURCES "${MOD_PLAYERBOTS_TEST_DIR}/*.cpp")

  if(MOD_PLAYERBOTS_TEST_SOURCES)
    set_property(GLOBAL APPEND PROPERTY ACORE_MODULE_TEST_SOURCES ${MOD_PLAYERBOTS_TEST_SOURCES})
    set_property(GLOBAL APPEND PROPERTY ACORE_MODULE_TEST_INCLUDES
      "${CMAKE_CURRENT_LIST_DIR}/src/Ai/Base/Util"
      "${CMAKE_CURRENT_LIST_DIR}/src/Ai/Base/Value"
      "${CMAKE_CURRENT_LIST_DIR}/src/Bot"
      "${CMAKE_CURRENT_LIST_DIR}/src/Bot/Engine"
    )
    message(STATUS "mod-playerbots: registered ${MOD_PLAYERBOTS_TEST_SOURCES} with unit_tests")
  endif()

  # Static registry consistency check. It needs no game state, so it runs as its own ctest entry and
  # parallelises with everything else under `ctest -j`. It fails only on findings that are not in the
  # committed baseline, so an existing backlog does not block the build.
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(
      NAME playerbots_action_registry
      COMMAND ${Python3_EXECUTABLE}
              "${CMAKE_CURRENT_LIST_DIR}/tools/check_action_registry.py"
              --src "${CMAKE_CURRENT_LIST_DIR}/src"
              --baseline "${CMAKE_CURRENT_LIST_DIR}/tools/action_registry_baseline.json"
    )
  endif()
endif()
