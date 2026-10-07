
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was SDL3Config.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

# SDL3 CMake Package Configuration for PlayStation 5
#
# This file provides configuration for finding and using the SDL3 library for PS5.
# It defines the following imported targets:
#   SDL3::SDL3-static   - The SDL3 static library for PlayStation 5
#   SDL3::SDL3          - Alias to SDL3::SDL3-static
#   SDL3::SDL3_test     - The SDL3 test framework library (if built)
#
# Example usage:
#   find_package(SDL3 REQUIRED CONFIG)
#   target_link_libraries(your_target PRIVATE SDL3::SDL3)

set(SDL3_FOUND TRUE)

# Find SDL3::SDL3-static
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/SDL3staticTargets.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/SDL3staticTargets.cmake")
    set(SDL3_SDL3-static_FOUND TRUE)
endif()

# Find SDL3::SDL3_test
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/SDL3testTargets.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/SDL3testTargets.cmake")
    set(SDL3_SDL3_test_FOUND TRUE)
endif()

# Check that we found at least the static library
if(SDL3_SDL3-static_FOUND)
    set(SDL3_SDL3_FOUND TRUE)
endif()

check_required_components(SDL3)

# Helper function to create target alias (compatible with CMake < 3.18)
function(_sdl_create_target_alias_compat NEW_TARGET TARGET)
    if(CMAKE_VERSION VERSION_LESS "3.18")
        # Aliasing local targets is not supported on CMake < 3.18, so make it global.
        add_library(${NEW_TARGET} INTERFACE IMPORTED)
        set_target_properties(${NEW_TARGET} PROPERTIES INTERFACE_LINK_LIBRARIES "${TARGET}")
    else()
        add_library(${NEW_TARGET} ALIAS ${TARGET})
    endif()
endfunction()

# Make sure SDL3::SDL3 always exists as an alias to SDL3-static
if(NOT TARGET SDL3::SDL3)
    if(TARGET SDL3::SDL3-static)
        _sdl_create_target_alias_compat(SDL3::SDL3 SDL3::SDL3-static)
    endif()
endif()

# Set convenience variables
set(SDL3_VERSION 3.2.0)
set(SDL3_LIBRARIES SDL3::SDL3)
set(SDL3_STATIC_LIBRARIES SDL3::SDL3-static)

if(TARGET SDL3::SDL3_test)
    set(SDL3TEST_LIBRARY SDL3::SDL3_test)
endif()

# Get the include directories from the imported target
if(TARGET SDL3::SDL3-static)
    get_target_property(SDL3_INCLUDE_DIRS SDL3::SDL3-static INTERFACE_INCLUDE_DIRECTORIES)
endif()

message(STATUS "Found SDL3 ${SDL3_VERSION} for PlayStation 5")
