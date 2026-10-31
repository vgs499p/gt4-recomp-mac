# macOS arm64 port: the host-side libraries of native/ and their controls, built with Apple Clang (or Homebrew
# LLVM) and Homebrew SDL3 / MoltenVK. The Windows-qualified build (native/CMakeLists.txt) is untouched; this file
# mirrors its targets one by one as each is ported. The game executable (gt4vulkan) still needs the AOT EE library
# regenerated for arm64 (docs/MACOS-PORT.md) and is not built here yet.
#
#   cmake -S . -B build/macos -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/macos
#   ctest --test-dir build/macos --output-on-failure

set(GT4_NATIVE "${CMAKE_SOURCE_DIR}/native")
set(GT4_WARNINGS -Wall -Wextra)

# Pure host libraries (same grouping as native/CMakeLists.txt).
add_library(gt4_pacing STATIC "${GT4_NATIVE}/pacing.cpp")
add_library(gt4_quickfix STATIC
    "${GT4_NATIVE}/window_mode.cpp" "${GT4_NATIVE}/race_display.cpp" "${GT4_NATIVE}/race_widescreen.cpp"
    "${GT4_NATIVE}/cursor_autohide.cpp" "${GT4_NATIVE}/display_aspect.cpp")
add_library(gt4_vulkan_extensions STATIC "${GT4_NATIVE}/vulkan_extensions.cpp")
add_library(gt4_install_paths STATIC "${GT4_NATIVE}/install_paths.cpp")
add_library(gt4_memcard STATIC "${GT4_NATIVE}/memcard_store.cpp")
add_library(gt4_license_guard STATIC "${GT4_NATIVE}/license_guard.cpp")
add_library(gt4_perf_probe STATIC "${GT4_NATIVE}/perf_probe.cpp")
add_library(gt4_mtvu_measure STATIC "${GT4_NATIVE}/mtvu_measure.cpp")
add_library(gt4_mtvu_journal STATIC "${GT4_NATIVE}/mtvu_journal.cpp")
add_library(gt4_mtvu_runtime STATIC "${GT4_NATIVE}/mtvu_runtime.cpp")
target_link_libraries(gt4_mtvu_runtime PUBLIC gt4_mtvu_journal gt4_mtvu_measure gt4_perf_probe gt4_quickfix)
add_library(gt4_pad_mapping STATIC "${GT4_NATIVE}/pad_mapping.cpp")
add_library(gt4_frame_clock STATIC "${GT4_NATIVE}/frame_clock.cpp")
foreach(library IN ITEMS gt4_pacing gt4_quickfix gt4_vulkan_extensions gt4_install_paths gt4_memcard gt4_license_guard
                         gt4_perf_probe gt4_mtvu_measure gt4_mtvu_journal gt4_mtvu_runtime gt4_pad_mapping
                         gt4_frame_clock)
    target_include_directories(${library} PUBLIC "${GT4_NATIVE}")
    target_compile_options(${library} PRIVATE ${GT4_WARNINGS})
endforeach()

# Host-only controls: (test name, executable, sources..., LINK libraries...).
function(gt4_fixture test_name target)
    cmake_parse_arguments(FIXTURE "" "" "SOURCES;LINK;ARGS" ${ARGN})
    add_executable(${target} ${FIXTURE_SOURCES})
    target_include_directories(${target} PRIVATE "${GT4_NATIVE}" "${GT4_NATIVE}/gs_stream")
    target_compile_options(${target} PRIVATE ${GT4_WARNINGS})
    if(FIXTURE_LINK)
        target_link_libraries(${target} PRIVATE ${FIXTURE_LINK})
    endif()
    add_test(NAME ${test_name} COMMAND $<TARGET_FILE:${target}> ${FIXTURE_ARGS})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 120 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        ENVIRONMENT "GT4_NATIVE_BUILD=${CMAKE_BINARY_DIR}")
endfunction()

gt4_fixture(native_memcard_store memcard_store_fixture SOURCES "${GT4_NATIVE}/memcard_store_fixture.cpp"
    LINK gt4_memcard ARGS "${CMAKE_BINARY_DIR}/memcard_store_work")
gt4_fixture(native_license_guard license_guard_fixture SOURCES "${GT4_NATIVE}/license_guard_fixture.cpp"
    LINK gt4_license_guard)
gt4_fixture(native_vulkan_extensions vulkan_extensions_fixture SOURCES "${GT4_NATIVE}/vulkan_extensions_fixture.cpp"
    LINK gt4_vulkan_extensions)
gt4_fixture(native_gs_submit_ring gs_submit_ring_fixture SOURCES "${GT4_NATIVE}/gs_submit_ring_fixture.cpp")
gt4_fixture(native_fpu_chop fpu_chop_fixture SOURCES "${GT4_NATIVE}/fpu_chop_fixture.cpp")
gt4_fixture(native_controls controls_fixture
    SOURCES "${GT4_NATIVE}/controls_fixture.cpp" "${GT4_NATIVE}/controls.cpp")
target_compile_definitions(controls_fixture PRIVATE GT4_CONTROLS_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/docs/controls")
gt4_fixture(native_pad_mapping pad_mapping_fixture
    SOURCES "${GT4_NATIVE}/pad_mapping_fixture.cpp" "${GT4_NATIVE}/controls.cpp" LINK gt4_pad_mapping)
gt4_fixture(native_pacing pacing_fixture SOURCES "${GT4_NATIVE}/pacing_fixture.cpp" LINK gt4_pacing)
gt4_fixture(native_quickfix quickfix_fixture SOURCES "${GT4_NATIVE}/quickfix_fixture.cpp" LINK gt4_quickfix)
gt4_fixture(native_race_display race_display_fixture SOURCES "${GT4_NATIVE}/race_display_fixture.cpp"
    LINK gt4_quickfix)
gt4_fixture(native_race_widescreen race_widescreen_fixture SOURCES "${GT4_NATIVE}/race_widescreen_fixture.cpp"
    LINK gt4_quickfix)
gt4_fixture(native_display_aspect display_aspect_fixture SOURCES "${GT4_NATIVE}/display_aspect_fixture.cpp"
    LINK gt4_quickfix)
gt4_fixture(native_cursor_autohide cursor_autohide_fixture SOURCES "${GT4_NATIVE}/cursor_autohide_fixture.cpp"
    LINK gt4_quickfix)
gt4_fixture(native_shape_observer shape_observer_fixture
    SOURCES "${GT4_NATIVE}/shape_observer_fixture.cpp" "${GT4_NATIVE}/shape_observer.cpp")
gt4_fixture(native_performance_accounting perf_accounting_fixture SOURCES "${GT4_NATIVE}/perf_accounting_fixture.cpp")
gt4_fixture(native_performance_probe perf_probe_fixture SOURCES "${GT4_NATIVE}/perf_probe_fixture.cpp"
    LINK gt4_perf_probe)
gt4_fixture(native_mtvu_measure mtvu_measure_fixture SOURCES "${GT4_NATIVE}/mtvu_measure_fixture.cpp"
    LINK gt4_mtvu_measure)
gt4_fixture(native_mtvu_ring mtvu_ring_fixture SOURCES "${GT4_NATIVE}/mtvu_ring_fixture.cpp")
gt4_fixture(native_mtvu_journal mtvu_journal_fixture SOURCES "${GT4_NATIVE}/mtvu_journal_fixture.cpp"
    LINK gt4_mtvu_journal)
gt4_fixture(native_mtvu_perf mtvu_perf_fixture SOURCES "${GT4_NATIVE}/mtvu_perf_fixture.cpp" LINK gt4_perf_probe)
gt4_fixture(native_mtvu_vif_batch mtvu_vif_batch_fixture SOURCES "${GT4_NATIVE}/mtvu_vif_batch_fixture.cpp")
gt4_fixture(native_mtvu_switches mtvu_switch_fixture SOURCES "${GT4_NATIVE}/mtvu_switch_fixture.cpp")
gt4_fixture(native_shape_capture_export shape_capture_export_fixture
    SOURCES "${GT4_NATIVE}/shape_capture_export_fixture.cpp" "${GT4_NATIVE}/shape_capture_export.cpp"
            "${GT4_NATIVE}/shape_observer.cpp")
gt4_fixture(native_shape_capture_control shape_capture_control_fixture
    SOURCES "${GT4_NATIVE}/shape_capture_control_fixture.cpp" "${GT4_NATIVE}/shape_capture_control.cpp"
            "${GT4_NATIVE}/shape_capture_export.cpp" "${GT4_NATIVE}/shape_observer.cpp")

# GS stream transport (TASK-035): POSIX shared memory and os_sync wake counters (gs_stream_ipc.hpp).
add_library(gt4_gs_stream STATIC "${GT4_NATIVE}/gs_stream/gs_stream_ipc.cpp" "${GT4_NATIVE}/gs_stream/gs_stream_file.cpp")
target_include_directories(gt4_gs_stream PUBLIC "${GT4_NATIVE}/gs_stream")
target_compile_options(gt4_gs_stream PRIVATE ${GT4_WARNINGS})
gt4_fixture(native_gs_stream_ipc gs_stream_ipc_fixture SOURCES "${GT4_NATIVE}/gs_stream/gs_stream_ipc_fixture.cpp"
    LINK gt4_gs_stream)
set_tests_properties(native_gs_stream_ipc PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)

# GS stream client (game side) and its control; gs_stream_record is the test consumer it launches. The client shows
# the player's dialogs with SDL3 message boxes (Homebrew sdl3 3.4.18, the version native/CMakeLists.txt pins).
find_package(SDL3 3.4.18 REQUIRED CONFIG)
add_library(gt4_gs_stream_client STATIC "${GT4_NATIVE}/gs_stream/gs_stream_client.cpp")
target_link_libraries(gt4_gs_stream_client PUBLIC gt4_gs_stream gt4_perf_probe gt4_install_paths gt4_quickfix
    SDL3::SDL3)
target_include_directories(gt4_gs_stream_client PUBLIC "${GT4_NATIVE}")
target_compile_options(gt4_gs_stream_client PRIVATE ${GT4_WARNINGS})
target_compile_definitions(gt4_gs_stream_client INTERFACE GT4_GS_STREAM_CLIENT)
add_executable(gs_stream_record "${CMAKE_SOURCE_DIR}/tools/gs_stream_record/gs_stream_record.cpp")
target_link_libraries(gs_stream_record PRIVATE gt4_gs_stream)
target_compile_options(gs_stream_record PRIVATE ${GT4_WARNINGS})
gt4_fixture(native_gs_stream_client gs_stream_client_fixture
    SOURCES "${GT4_NATIVE}/gs_stream/gs_stream_client_fixture.cpp" LINK gt4_gs_stream_client
    ARGS $<TARGET_FILE:gs_stream_record> "${CMAKE_BINARY_DIR}/gs-stream-client-scratch")
set_tests_properties(native_gs_stream_client PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)

# Host audio and input (host_io.cpp): SDL3 gamepads and an SDL3 audio stream on macOS. The control plays silence on
# the default output device for about five seconds.
add_library(native_host_io STATIC "${GT4_NATIVE}/host_io.cpp" "${GT4_NATIVE}/controls.cpp")
target_link_libraries(native_host_io PUBLIC gt4_pad_mapping gt4_pacing gt4_frame_clock SDL3::SDL3)
target_include_directories(native_host_io PUBLIC "${GT4_NATIVE}")
target_compile_options(native_host_io PRIVATE ${GT4_WARNINGS})
gt4_fixture(native_audio_input host_io_fixture SOURCES "${GT4_NATIVE}/host_io_fixture.cpp" LINK native_host_io)
set_tests_properties(native_audio_input PROPERTIES RUN_SERIAL TRUE)
