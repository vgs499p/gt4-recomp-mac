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
    "${GT4_NATIVE}/frame_skip.cpp" "${GT4_NATIVE}/window_mode.cpp" "${GT4_NATIVE}/race_display.cpp"
    "${GT4_NATIVE}/race_widescreen.cpp" "${GT4_NATIVE}/cursor_autohide.cpp" "${GT4_NATIVE}/display_aspect.cpp"
    "${GT4_NATIVE}/flare_limits.cpp" "${GT4_NATIVE}/camera_360.cpp" "${GT4_NATIVE}/fov_adjust.cpp"
    "${GT4_NATIVE}/trigger_range.cpp")
# TASK-018: the USB controller model and the virtual Driving Force Pro (the USB capsule and the wheel).
add_library(gt4_usb STATIC "${GT4_NATIVE}/usb_ohci.cpp" "${GT4_NATIVE}/usb_dfp.cpp")
add_library(gt4_vulkan_extensions STATIC "${GT4_NATIVE}/vulkan_extensions.cpp")
add_library(gt4_install_paths STATIC "${GT4_NATIVE}/install_paths.cpp")
add_library(gt4_memcard STATIC "${GT4_NATIVE}/memcard_store.cpp")
add_library(gt4_license_guard STATIC "${GT4_NATIVE}/license_guard.cpp")
add_library(gt4_perf_probe STATIC "${GT4_NATIVE}/perf_probe.cpp" "${GT4_NATIVE}/perf_clock.cpp")
add_library(gt4_mtvu_measure STATIC "${GT4_NATIVE}/mtvu_measure.cpp")
add_library(gt4_mtvu_journal STATIC "${GT4_NATIVE}/mtvu_journal.cpp")
add_library(gt4_mtvu_runtime STATIC "${GT4_NATIVE}/mtvu_runtime.cpp")
target_link_libraries(gt4_mtvu_runtime PUBLIC gt4_mtvu_journal gt4_mtvu_measure gt4_perf_probe gt4_quickfix)
add_library(gt4_pad_mapping STATIC "${GT4_NATIVE}/pad_mapping.cpp" "${GT4_NATIVE}/pad_choice.cpp"
    "${GT4_NATIVE}/input_recording.cpp")
add_library(gt4_frame_clock STATIC "${GT4_NATIVE}/frame_clock.cpp")
foreach(library IN ITEMS gt4_pacing gt4_quickfix gt4_vulkan_extensions gt4_install_paths gt4_memcard gt4_license_guard
                         gt4_perf_probe gt4_mtvu_measure gt4_mtvu_journal gt4_mtvu_runtime gt4_pad_mapping
                         gt4_frame_clock gt4_usb)
    target_include_directories(${library} PUBLIC "${GT4_NATIVE}")
    target_compile_options(${library} PRIVATE ${GT4_WARNINGS})
endforeach()

# Host-only controls: (test name, executable, sources..., LINK libraries...).
function(gt4_fixture test_name target)
    cmake_parse_arguments(FIXTURE "" "" "SOURCES;LINK;ARGS" ${ARGN})
    add_executable(${target} ${FIXTURE_SOURCES})
    target_include_directories(${target} PRIVATE "${GT4_NATIVE}" "${GT4_NATIVE}/gs_stream")
    # The shared fixtures call a few Win32 environment and timing functions (_putenv_s, Sleep, ...): the POSIX shims.
    target_compile_options(${target} PRIVATE ${GT4_WARNINGS} $<$<COMPILE_LANGUAGE:CXX>:-include "${GT4_NATIVE}/posix_compat.hpp">)
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
# Upstream's controls since the 2026-11-16 sync (the game-patch hooks, frame skip, USB wheel, input recording, the
# standalone start, the game target table, the SPU2 envelope and the fast IPU forms).
gt4_fixture(native_camera_360 camera_360_fixture SOURCES "${GT4_NATIVE}/camera_360_fixture.cpp" LINK gt4_quickfix)
gt4_fixture(native_fov_adjust fov_adjust_fixture SOURCES "${GT4_NATIVE}/fov_adjust_fixture.cpp" LINK gt4_quickfix)
gt4_fixture(native_trigger_range trigger_range_fixture SOURCES "${GT4_NATIVE}/trigger_range_fixture.cpp"
    LINK gt4_quickfix)
gt4_fixture(native_flare_limits flare_limits_fixture SOURCES "${GT4_NATIVE}/flare_limits_fixture.cpp" LINK gt4_quickfix)
gt4_fixture(native_frame_skip frame_skip_fixture SOURCES "${GT4_NATIVE}/frame_skip_fixture.cpp"
    "${GT4_NATIVE}/frame_skip.cpp" LINK gt4_pacing)
gt4_fixture(native_usb_dfp usb_dfp_fixture SOURCES "${GT4_NATIVE}/usb_dfp_fixture.cpp" LINK gt4_usb)
gt4_fixture(native_input_recording input_recording_fixture SOURCES "${GT4_NATIVE}/input_recording_fixture.cpp"
    LINK gt4_pad_mapping ARGS "${CMAKE_BINARY_DIR}/native_input_recording.txt")
# native_standalone is not built: its samples are Windows install folders (drive letters, backslashes, 8.3 names), and on
# macOS a start without the launcher shows a message instead of running those rules (native/game_main.cpp).
gt4_fixture(native_game_target game_target_fixture SOURCES "${GT4_NATIVE}/game_target_fixture.cpp"
    "${GT4_NATIVE}/game_target_spec2_fixture.cpp")
set_source_files_properties("${GT4_NATIVE}/game_target_fixture.cpp" PROPERTIES COMPILE_DEFINITIONS GT4_TARGET_FIXTURE_V200)
gt4_fixture(native_spu2_envelope spu2_envelope_fixture SOURCES "${GT4_NATIVE}/spu2_envelope_fixture.cpp")
gt4_fixture(native_ipu_fast ipu_fast_fixture SOURCES "${GT4_NATIVE}/ipu_fast_fixture.cpp")
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
add_library(gt4_gs_stream STATIC "${GT4_NATIVE}/gs_stream/gs_stream_ipc.cpp" "${GT4_NATIVE}/gs_stream/gs_stream_file.cpp"
    "${GT4_NATIVE}/gs_stream/gs_readback_probe.cpp")
target_include_directories(gt4_gs_stream PUBLIC "${GT4_NATIVE}/gs_stream")
target_compile_options(gt4_gs_stream PRIVATE ${GT4_WARNINGS})
gt4_fixture(native_gs_stream_ipc gs_stream_ipc_fixture SOURCES "${GT4_NATIVE}/gs_stream/gs_stream_ipc_fixture.cpp"
    LINK gt4_gs_stream)
set_tests_properties(native_gs_stream_ipc PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)

# GS stream client (game side) and its control; gs_stream_record is the test consumer it launches. The client shows
# the player's dialogs with SDL3 message boxes (Homebrew sdl3 3.4.18, the version native/CMakeLists.txt pins).
find_package(SDL3 3.4.18 REQUIRED CONFIG)
add_library(gt4_gs_stream_client STATIC "${GT4_NATIVE}/gs_stream/gs_stream_client.cpp"
    "${GT4_NATIVE}/gs_stream/gs_layer_host_mac.mm")
set_source_files_properties("${GT4_NATIVE}/gs_stream/gs_layer_host_mac.mm" PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_link_libraries(gt4_gs_stream_client PUBLIC "-framework AppKit" "-framework QuartzCore")
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
add_library(native_host_io STATIC "${GT4_NATIVE}/host_io.cpp" "${GT4_NATIVE}/controls.cpp" "${GT4_NATIVE}/wheel.cpp")
target_link_libraries(native_host_io PUBLIC gt4_pad_mapping gt4_usb gt4_pacing gt4_frame_clock gt4_install_paths SDL3::SDL3)
target_include_directories(native_host_io PUBLIC "${GT4_NATIVE}")
target_compile_options(native_host_io PRIVATE ${GT4_WARNINGS})
gt4_fixture(native_audio_input host_io_fixture SOURCES "${GT4_NATIVE}/host_io_fixture.cpp" LINK native_host_io)
gt4_fixture(native_wheel wheel_fixture SOURCES "${GT4_NATIVE}/wheel_fixture.cpp" LINK native_host_io)
set_tests_properties(native_audio_input PROPERTIES RUN_SERIAL TRUE)

# RetroAchievements (achievements.cpp with the vendored rcheevos): web requests through the system libcurl
# (achievements_http_curl.cpp) and the overlay as a child NSWindow drawn with CoreGraphics/CoreText
# (achievements_overlay_mac.mm).
enable_language(C)
enable_language(OBJCXX)
find_package(CURL REQUIRED)
file(GLOB RCHEEVOS_SOURCES CONFIGURE_DEPENDS
    "${GT4_NATIVE}/third_party/rcheevos/src/*.c"
    "${GT4_NATIVE}/third_party/rcheevos/src/api/*.c"
    "${GT4_NATIVE}/third_party/rcheevos/src/client/*.c"
    "${GT4_NATIVE}/third_party/rcheevos/src/hash/*.c"
    "${GT4_NATIVE}/third_party/rcheevos/src/runtime/*.c"
    "${GT4_NATIVE}/third_party/rcheevos/src/util/*.c")
add_library(gt4_rcheevos STATIC ${RCHEEVOS_SOURCES})
target_include_directories(gt4_rcheevos PUBLIC "${GT4_NATIVE}/third_party/rcheevos/include")
target_compile_definitions(gt4_rcheevos PUBLIC RC_CLIENT_SUPPORTS_HASH)
add_library(gt4_achievements STATIC "${GT4_NATIVE}/achievements.cpp" "${GT4_NATIVE}/achievements_overlay.cpp"
    "${GT4_NATIVE}/achievements_overlay_mac.mm" "${GT4_NATIVE}/achievements_http_curl.cpp")
target_include_directories(gt4_achievements PUBLIC "${GT4_NATIVE}")
target_link_libraries(gt4_achievements PUBLIC gt4_rcheevos SDL3::SDL3 CURL::libcurl
    "-framework AppKit" "-framework CoreText" "-framework ImageIO" "-framework QuartzCore")
target_compile_options(gt4_achievements PRIVATE ${GT4_WARNINGS} $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
gt4_fixture(native_achievements achievements_fixture SOURCES "${GT4_NATIVE}/achievements_fixture.cpp"
    LINK gt4_achievements)
gt4_fixture(native_achievements_http achievements_http_fixture SOURCES "${GT4_NATIVE}/achievements_http_fixture.cpp"
    LINK gt4_achievements)

# Donor runtime and the native Vulkan GS backend (MoltenVK) with the pixel and async-stress controls. Needs the MIT
# donor checked out with CRLF line endings (the pins are Windows checkouts), the port derived from it and the SPIR-V
# shaders (docs/MACOS-PORT.md). The port and shaders must match the slice191 pins; the port is compared with LF line
# endings turned into CRLF, the only difference of a POSIX derivation.
set(GT4_DONOR "${CMAKE_SOURCE_DIR}/private/references/ps2-web-recomp" CACHE PATH "MIT donor checkout (CRLF)")
set(GT4_MACOS_PORT_DIR "${CMAKE_SOURCE_DIR}/private/macos-port" CACHE PATH "prepare_external_vulkan.py --gs-stream-hooks output")
set(GT4_MACOS_SHADER_DIR "${CMAKE_SOURCE_DIR}/private/macos-shaders" CACHE PATH "gt4-wgsl-spirv output")
if(NOT EXISTS "${GT4_MACOS_PORT_DIR}/gpu.cpp" OR NOT EXISTS "${GT4_MACOS_SHADER_DIR}/raster.spv" OR
   NOT EXISTS "${GT4_DONOR}/runtime/src/memory.cpp")
    message(STATUS "Vulkan GS backend skipped: no donor, port or shaders (docs/MACOS-PORT.md)")
    return()
endif()
file(READ "${CMAKE_SOURCE_DIR}/docs/inputs/slice191-gs-stream-runtime.json" macos_pins)
foreach(port_file IN ITEMS gpu.cpp gpu.hpp gs.cpp)
    file(READ "${GT4_MACOS_PORT_DIR}/${port_file}" port_text)
    string(REPLACE "\r\n" "\n" port_text "${port_text}")
    string(REPLACE "\n" "\r\n" port_text "${port_text}")
    string(SHA256 port_actual "${port_text}")
    string(JSON port_expected GET "${macos_pins}" inputs PORT "${port_file}")
    if(NOT port_actual STREQUAL port_expected)
        message(FATAL_ERROR "${GT4_MACOS_PORT_DIR}/${port_file} differs from the slice191 pin")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GT4_MACOS_PORT_DIR}/${port_file}")
endforeach()
foreach(shader IN ITEMS upload copy_read copy_write load_clut display raster resolve snapshot decode)
    file(SHA256 "${GT4_MACOS_SHADER_DIR}/${shader}.spv" shader_actual)
    string(JSON shader_expected GET "${macos_pins}" inputs SHADERS "${shader}.spv")
    if(NOT shader_actual STREQUAL shader_expected)
        message(FATAL_ERROR "${GT4_MACOS_SHADER_DIR}/${shader}.spv differs from the slice191 pin")
    endif()
endforeach()
find_package(VulkanHeaders CONFIG REQUIRED)
set(macos_runtime_files)
foreach(source IN ITEMS memory dispatch system kernel hw cop disc dmac vif vu_micro vu_jit vu_jit_web
                        ipu/ipu gs/gif gs/queue gs/raster iop/cpu iop/kernel iop/libs iop/hw iop/spu2 iop/sio2
                        iop/io iop/cdvd iop/sif iop/cdvdfsv)
    list(APPEND macos_runtime_files "${GT4_DONOR}/runtime/src/${source}.cpp")
endforeach()
# The release runtime of upstream 1.3.0-rc14 (docs/inputs/slice408-pcsx2.json, RC14_PIN below), rebuilt on macOS from the
# owner's disc and the pinned donor; every input is checked here against the pin that names it (docs/MACOS-PORT.md):
#   private/macos-runtime        prepare_perf_runtime.py --gs-stream --frame-cost      slice262-frame-cost-runtime.json
#   private/macos-vu             ports/macos/vu_generate.py (VU1 and VU0 tables)       slice408-pcsx2.json
#   private/macos-license-guard  prepare_license_guard.py --display-aspect --frame-counters
#                                (the per-target guard of slice277, as the Spec II release; v2.00 rc14 used slice262)
#   private/macos-memcard        prepare_memcard_flush.py                               slice408-pcsx2.json
#   private/macos-ipu            prepare_ipu_fast.py                                    slice408-pcsx2.json (receipt)
#   private/macos-spu2           prepare_spu2_runtime.py                                slice408-pcsx2.json (receipt)
# Without the runtime capsule and the VU tables the plain donor runtime is built (VU1 and VU0 interpreted, one thread).
set(RC14_PIN "${CMAKE_SOURCE_DIR}/docs/inputs/slice408-pcsx2.json")
set(RC14_SPEC2_PIN "${CMAKE_SOURCE_DIR}/docs/inputs/slice408-spec2.json")
file(READ "${RC14_PIN}" rc14_pin)
file(READ "${RC14_SPEC2_PIN}" rc14_spec2_pin)
set(GT4_MACOS_RUNTIME_CAPSULE "${CMAKE_SOURCE_DIR}/private/macos-runtime" CACHE PATH
    "prepare_perf_runtime.py --gs-stream --frame-cost output")
set(GT4_MACOS_VU_DIR "${CMAKE_SOURCE_DIR}/private/macos-vu" CACHE PATH "ports/macos/vu_generate.py output")
set(GT4_MACOS_RELEASE_RUNTIME OFF)

# Replaces the donor source `donor_relative` (e.g. "iop/sio2.cpp") with `file` after checking its SHA-256.
function(gt4_capsule_source donor_relative file expected label)
    file(SHA256 "${file}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "${file} differs from the ${label} pin")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
    set(files ${macos_runtime_files})
    list(REMOVE_ITEM files "${GT4_DONOR}/runtime/src/${donor_relative}")
    list(APPEND files "${file}")
    set(macos_runtime_files ${files} PARENT_SCOPE)
endfunction()

if(EXISTS "${GT4_MACOS_RUNTIME_CAPSULE}/runtime.json" AND EXISTS "${GT4_MACOS_VU_DIR}/vu1-gen/vu1_generated.cpp")
    set(GT4_MACOS_RELEASE_RUNTIME ON)
    file(READ "${CMAKE_SOURCE_DIR}/docs/inputs/slice262-frame-cost-runtime.json" release_runtime_manifest)
    foreach(source IN ITEMS system vif vu_micro gif memory hw dmac)
        string(JSON expected GET "${release_runtime_manifest}" source_sha256 "${source}.cpp")
        if(source STREQUAL "gif")
            gt4_capsule_source("gs/gif.cpp" "${GT4_MACOS_RUNTIME_CAPSULE}/gif.cpp" "${expected}" "slice262 runtime")
        else()
            gt4_capsule_source("${source}.cpp" "${GT4_MACOS_RUNTIME_CAPSULE}/${source}.cpp" "${expected}" "slice262 runtime")
        endif()
    endforeach()
    string(JSON vu1_table_pin GET "${rc14_pin}" vu1_aot generated_source_sha256)
    string(JSON vu0_table_pin GET "${rc14_pin}" vu0_aot table_sha256)
    file(SHA256 "${GT4_MACOS_VU_DIR}/vu1-gen/vu1_generated.cpp" vu1_table_actual)
    file(SHA256 "${GT4_MACOS_VU_DIR}/vu0-gen/vu0_generated.cpp" vu0_table_actual)
    if(NOT vu1_table_actual STREQUAL vu1_table_pin OR NOT vu0_table_actual STREQUAL vu0_table_pin)
        message(FATAL_ERROR "The VU1/VU0 tables in ${GT4_MACOS_VU_DIR} differ from the rc14 pins")
    endif()
    file(GLOB vu1_generated_parts CONFIGURE_DEPENDS "${GT4_MACOS_VU_DIR}/vu1-gen/vu1_generated_*.cpp")
    file(GLOB vu0_generated_parts CONFIGURE_DEPENDS "${GT4_MACOS_VU_DIR}/vu0-gen/vu0_generated_*.cpp")
    list(SORT vu1_generated_parts)
    list(SORT vu0_generated_parts)
    set(vu_generated_sources "${GT4_MACOS_VU_DIR}/vu1-gen/vu1_generated.cpp" ${vu1_generated_parts}
        "${GT4_MACOS_VU_DIR}/vu0-gen/vu0_generated.cpp" ${vu0_generated_parts})
    set_source_files_properties(${vu_generated_sources} PROPERTIES COMPILE_OPTIONS "-Wno-unused-label")
    list(APPEND macos_runtime_files ${vu_generated_sources} "${CMAKE_SOURCE_DIR}/tools/vu_recomp/src/sha256.cpp"
        "${GT4_NATIVE}/mtvu_bridge.cpp")
    message(STATUS "macOS port: rc14 release runtime (MTVU, VU1 and VU0 AOT, frame-cost hooks)")
endif()

# TASK-058 crash-safe memory card: the derived iop/sio2.cpp (atomic writer, native/memcard_store.cpp).
set(GT4_MACOS_MEMCARD "${CMAKE_SOURCE_DIR}/private/macos-memcard" CACHE PATH "prepare_memcard_flush.py output")
set(GT4_MACOS_MEMCARD_ENABLED OFF)
if(EXISTS "${GT4_MACOS_MEMCARD}/sio2.cpp")
    string(JSON expected GET "${rc14_pin}" memcard_capsule_sha256 "sio2.cpp")
    gt4_capsule_source("iop/sio2.cpp" "${GT4_MACOS_MEMCARD}/sio2.cpp" "${expected}" "rc14 memory card")
    set_source_files_properties("${GT4_MACOS_MEMCARD}/sio2.cpp" PROPERTIES INCLUDE_DIRECTORIES "${GT4_DONOR}/runtime/src/iop")
    set(GT4_MACOS_MEMCARD_ENABLED ON)
endif()

# TASK-027 IN-490 IPU forms and TASK-125 SPU2 envelope: each capsule's runtime.json must be the rc14 receipt, and the
# derived source must be the one that receipt names.
foreach(capsule IN ITEMS ipu spu2)
    set(capsule_dir "${CMAKE_SOURCE_DIR}/private/macos-${capsule}")
    if(capsule STREQUAL "ipu")
        set(donor_relative "ipu/ipu.cpp")
    else()
        set(donor_relative "iop/spu2.cpp")
    endif()
    if(EXISTS "${capsule_dir}/runtime.json")
        file(SHA256 "${capsule_dir}/runtime.json" receipt_actual)
        string(JSON receipt_expected GET "${rc14_pin}" "${capsule}_capsule_sha256")
        if(NOT receipt_actual STREQUAL receipt_expected)
            message(FATAL_ERROR "${capsule_dir}/runtime.json differs from the rc14 pin")
        endif()
        file(READ "${capsule_dir}/runtime.json" receipt)
        string(JSON source_expected GET "${receipt}" "${capsule}_sha256")
        get_filename_component(source_name "${donor_relative}" NAME)
        gt4_capsule_source("${donor_relative}" "${capsule_dir}/${source_name}" "${source_expected}" "rc14 ${capsule}")
        get_filename_component(donor_folder "${GT4_DONOR}/runtime/src/${donor_relative}" DIRECTORY)
        set_source_files_properties("${capsule_dir}/${source_name}" PROPERTIES
            INCLUDE_DIRECTORIES "${donor_folder};${GT4_DONOR}/runtime/src;${GT4_NATIVE}")
    endif()
endforeach()

# TASK-017 license demo guard with the TASK-080 aspect hooks and the TASK-084 frame counters: the per-target guard
# (slice277, pinned by the Spec II release) replaces dispatch.cpp, kernel.cpp and iop/cdvdfsv.cpp. Without it a license
# test crashes (docs/LICENSE-DEMO-CIPHER.md).
set(GT4_MACOS_LICENSE_GUARD "${CMAKE_SOURCE_DIR}/private/macos-license-guard" CACHE PATH
    "prepare_license_guard.py --display-aspect --frame-counters output")
set(GT4_MACOS_LICENSE_GUARD_ENABLED OFF)
if(EXISTS "${GT4_MACOS_LICENSE_GUARD}/runtime.json")
    foreach(source IN ITEMS cdvdfsv dispatch kernel)
        string(JSON expected GET "${rc14_spec2_pin}" license_guard_source_sha256 "${source}.cpp")
        if(source STREQUAL "cdvdfsv")
            gt4_capsule_source("iop/cdvdfsv.cpp" "${GT4_MACOS_LICENSE_GUARD}/cdvdfsv.cpp" "${expected}" "slice277 guard")
        else()
            gt4_capsule_source("${source}.cpp" "${GT4_MACOS_LICENSE_GUARD}/${source}.cpp" "${expected}" "slice277 guard")
        endif()
    endforeach()
    set(GT4_MACOS_LICENSE_GUARD_ENABLED ON)
    message(STATUS "macOS port: license demo guard (slice277, with the aspect hooks)")
endif()
add_library(ps2runtime_vulkan STATIC ${macos_runtime_files} "${GT4_MACOS_PORT_DIR}/gs.cpp"
    "${GT4_MACOS_PORT_DIR}/gpu.cpp" "${GT4_NATIVE}/native_vulkan.cpp")
if(GT4_MACOS_RELEASE_RUNTIME)
    target_include_directories(ps2runtime_vulkan PUBLIC "${CMAKE_SOURCE_DIR}/tools/vu_recomp/src")
    target_compile_definitions(gt4_perf_probe PUBLIC GT4_PERF_VU_SPLIT GT4_VU1_AOT GT4_VU0_AOT)
    target_compile_definitions(ps2runtime_vulkan PUBLIC GT4_MTVU_RUNTIME GT4_PERF_PROBE)
    target_link_libraries(ps2runtime_vulkan PUBLIC gt4_mtvu_runtime gt4_perf_probe)
endif()
if(GT4_MACOS_LICENSE_GUARD_ENABLED)
    target_link_libraries(ps2runtime_vulkan PUBLIC gt4_license_guard)
endif()
if(GT4_MACOS_MEMCARD_ENABLED)
    target_link_libraries(ps2runtime_vulkan PUBLIC gt4_memcard)
endif()
# TASK-134: the donor's runtime headers with the exact faster forms (scripts/prepare_fast_headers.py), first on the include
# path of the runtime and of the game code; the VU tables include ps2/fp4.hpp from them. native/fp4.hpp has a lane-by-lane
# form for processors without SSE4.1, which arm64 uses.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(GT4_FAST_HEADERS_DIR "${CMAKE_BINARY_DIR}/fast_headers")
execute_process(COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_SOURCE_DIR}/scripts/prepare_fast_headers.py" "${GT4_DONOR}"
    "${GT4_FAST_HEADERS_DIR}" RESULT_VARIABLE fast_headers_result)
if(NOT fast_headers_result EQUAL 0)
    message(FATAL_ERROR "scripts/prepare_fast_headers.py failed")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/scripts/prepare_fast_headers.py"
    "${GT4_NATIVE}/fp4.hpp")
target_include_directories(ps2runtime_vulkan BEFORE PUBLIC "${GT4_FAST_HEADERS_DIR}")
gt4_fixture(native_vu_fast_ops vu_fast_ops_fixture SOURCES "${GT4_NATIVE}/vu_fast_ops_fixture.cpp")
target_include_directories(vu_fast_ops_fixture PRIVATE "${GT4_FAST_HEADERS_DIR}" "${CMAKE_SOURCE_DIR}/tools/vu_recomp/src")
target_include_directories(ps2runtime_vulkan PUBLIC "${GT4_DONOR}/runtime/include" "${GT4_DONOR}/runtime/src"
    "${GT4_DONOR}/runtime/src/gs" "${GT4_NATIVE}" "${GT4_NATIVE}/gs_stream" "${GT4_MACOS_PORT_DIR}")
target_compile_definitions(ps2runtime_vulkan PUBLIC PS2_GPU GT4_VULKAN_SHADER_DIR="${GT4_MACOS_SHADER_DIR}")
target_link_libraries(ps2runtime_vulkan PUBLIC Vulkan::Headers gt4_pacing gt4_quickfix gt4_install_paths
    gt4_vulkan_extensions gt4_gs_stream_client gt4_usb SDL3::SDL3)
add_executable(vulkan_fixture "${GT4_NATIVE}/fixture.cpp")
target_link_libraries(vulkan_fixture PRIVATE ps2runtime_vulkan)
add_executable(vulkan_async_stress "${GT4_NATIVE}/async_stress_fixture.cpp")
target_link_libraries(vulkan_async_stress PRIVATE ps2runtime_vulkan)
add_test(NAME native_vulkan_pixels COMMAND $<TARGET_FILE:vulkan_fixture>)
add_test(NAME native_vulkan_pixels_sync COMMAND $<TARGET_FILE:vulkan_fixture>)
add_test(NAME native_vulkan_pixels_baseline COMMAND $<TARGET_FILE:vulkan_fixture>)
set_tests_properties(native_vulkan_pixels native_vulkan_pixels_sync native_vulkan_pixels_baseline PROPERTIES
    RUN_SERIAL TRUE TIMEOUT 120 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(native_vulkan_pixels_sync PROPERTIES ENVIRONMENT "GT4_GS_SYNC=1")
set_tests_properties(native_vulkan_pixels_baseline PROPERTIES ENVIRONMENT "GT4_VULKAN_FORCE_BASELINE=1"
    PASS_REGULAR_EXPRESSION "presentation path: baseline [^\n]*forced by GT4_VULKAN_FORCE_BASELINE=1.*PASS: real Vulkan")
add_test(NAME native_vulkan_async_stress_async
    COMMAND $<TARGET_FILE:vulkan_async_stress> "${CMAKE_BINARY_DIR}/async-stress-async.bin")
add_test(NAME native_vulkan_async_stress_sync
    COMMAND $<TARGET_FILE:vulkan_async_stress> "${CMAKE_BINARY_DIR}/async-stress-sync.bin")
add_test(NAME native_vulkan_async_stress_compare COMMAND ${CMAKE_COMMAND} -E compare_files
    "${CMAKE_BINARY_DIR}/async-stress-async.bin" "${CMAKE_BINARY_DIR}/async-stress-sync.bin")
set_tests_properties(native_vulkan_async_stress_async native_vulkan_async_stress_sync PROPERTIES
    RUN_SERIAL TRUE TIMEOUT 600 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(native_vulkan_async_stress_async PROPERTIES ENVIRONMENT "GT4_GS_SYNC=0")
set_tests_properties(native_vulkan_async_stress_sync PROPERTIES ENVIRONMENT "GT4_GS_SYNC=1")
set_tests_properties(native_vulkan_async_stress_compare PROPERTIES DEPENDS
    "native_vulkan_async_stress_async;native_vulkan_async_stress_sync")

# The game window (window_sdl.cpp, the SDL3 counterpart of window.cpp). It links only into the game executable, which
# needs the arm64 EE library (milestone 2); until then it is compiled here so it stays in step with the runtime API.
add_library(gt4_window STATIC "${GT4_NATIVE}/window_sdl.cpp")
target_link_libraries(gt4_window PUBLIC ps2runtime_vulkan native_host_io gt4_achievements gt4_gs_stream_client)
target_compile_definitions(gt4_window PRIVATE GT4_GS_STREAM_CLIENT GT4_PERF_PROBE GT4_MTVU_MEASURE)
target_link_libraries(gt4_window PUBLIC gt4_perf_probe gt4_mtvu_measure)
target_compile_options(gt4_window PRIVATE ${GT4_WARNINGS})

# The EE game library and the game executable. The library is compiled from the owner's disc: the donor translator,
# built for macOS, translates the TASK-080 aspect derivative of the ELF that scripts/ee_producer.py reconstructs
# (ports/macos/ee_generate.py --profile aspect, equal to the slice217 inventory), then scripts/camera360_units.py adds the
# camera, FoV, trigger and overlay hook sites; the receipt's output inventory must equal the rc14 pin. Without them the
# game targets are skipped (docs/MACOS-PORT.md).
set(GT4_MACOS_EE_GENERATED "${CMAKE_SOURCE_DIR}/private/macos-ee-hooked" CACHE PATH
    "scripts/camera360_units.py v200 output (table.cpp, units.hpp, unit_*.cpp), with <folder>.camera360-units.json")
if(NOT EXISTS "${GT4_MACOS_EE_GENERATED}/table.cpp" OR NOT EXISTS "${GT4_MACOS_EE_GENERATED}/units.hpp")
    message(STATUS "macOS port: the game (ps2game, gt4vulkan) is skipped; generate the EE library from your disc first")
    return()
endif()
file(READ "${GT4_MACOS_EE_GENERATED}.camera360-units.json" hooked_receipt)
string(JSON hooked_actual GET "${hooked_receipt}" output_inventory_sha256)
string(JSON hooked_expected GET "${rc14_pin}" camera360_units output_inventory_sha256)
if(NOT hooked_actual STREQUAL hooked_expected)
    message(FATAL_ERROR "${GT4_MACOS_EE_GENERATED} is not the rc14 hooked translation")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GT4_MACOS_EE_GENERATED}.camera360-units.json")
file(GLOB macos_generated_sources CONFIGURE_DEPENDS "${GT4_MACOS_EE_GENERATED}/*.cpp")
add_library(ps2game STATIC ${macos_generated_sources})
target_include_directories(ps2game BEFORE PRIVATE "${GT4_FAST_HEADERS_DIR}")
target_include_directories(ps2game PRIVATE "${GT4_MACOS_EE_GENERATED}" "${GT4_DONOR}/runtime/include")
target_compile_options(ps2game PRIVATE -Wno-unused-label -Wno-unused-variable -Wno-unused-but-set-variable)

# TASK-097: the entry point is native/game_main.cpp; the derived donor main.cpp is compiled with `main` renamed and called
# from it (with an ELF argument as before; without one, after the standalone rules of standalone.cpp).
set_source_files_properties("${GT4_MACOS_PORT_DIR}/main.cpp" PROPERTIES COMPILE_DEFINITIONS "main=gt4_donor_main")
add_executable(gt4vulkan "${GT4_MACOS_PORT_DIR}/main.cpp" "${GT4_NATIVE}/game_main.cpp" "${GT4_NATIVE}/standalone.cpp"
    "${GT4_NATIVE}/camera_360_hook.cpp" "${GT4_NATIVE}/fov_adjust_hook.cpp" "${GT4_NATIVE}/display_aspect_hook.cpp")
target_include_directories(gt4vulkan PRIVATE "${GT4_MACOS_PORT_DIR}" "${GT4_NATIVE}")
target_compile_definitions(gt4vulkan PRIVATE PS2_WINDOW)
if(GT4_MACOS_MEMCARD_ENABLED)
    target_compile_definitions(gt4vulkan PRIVATE GT4_MEMCARD_FLUSH_OVERLAY)
    target_compile_definitions(gt4_window PRIVATE GT4_MEMCARD_FLUSH_OVERLAY)
endif()
target_link_libraries(gt4vulkan PRIVATE ps2game gt4_window)
# The derived main.cpp calls _putenv_s; the POSIX shims are force-included so the derived source stays byte-identical.
target_compile_options(gt4vulkan PRIVATE -include "${GT4_NATIVE}/posix_compat.hpp")
