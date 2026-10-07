# PlayStation 5 system stub libraries, shared by the SDL3 examples.
#
# Include it with OPTIONAL, so an example directory still configures on its
# own when this file hasn't been copied along with it:
#
#   include(${CMAKE_CURRENT_LIST_DIR}/../ps5-libs.cmake OPTIONAL)
#   if(COMMAND ps5_link_system_libs)
#     ps5_link_system_libs(${n})
#   endif()
#
# Call it after the target links SDL3: the stubs must follow the archive
# that references them.

function(ps5_link_system_libs target)
  target_link_libraries(${target} PRIVATE
    -lScePosix_stub_weak
    -lSceUserService_stub_weak
    -lSceSaveData_stub_weak
    -lSceSaveDataDialog_stub_weak
    -lSceCommonDialog_stub_weak
    -lSceSystemService_stub_weak
    -lSceKeyboard_stub_weak
    -lSceImeDialog_stub_weak
    -lSceSysmodule_stub_weak
    -lSceAudioIn_stub_weak
    -lSceAudioOut_stub_weak
    -lSceAgcDriver_stub_weak
    -lSceAgc_stub_weak
    -lSceVideoOut_stub_weak
    -lScePad_stub_weak
    -lSceAgc
    -lSceAgcCore
    -lSceAgcGpuAddress
    -lSceNpUniversalDataSystem_stub_weak
  )
endfunction()
