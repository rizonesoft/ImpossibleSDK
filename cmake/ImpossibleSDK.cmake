# The one include a program needs, in its top CMakeLists.txt after project():
#
#   include("sdk/cmake/ImpossibleSDK.cmake")
#   impossible_add_program(MyProgram WINDOW TIER 1 SOURCES src/main.c)
#
# The build fails when the program imports anything outside its tier.
include_guard(GLOBAL)
get_filename_component(IMPOSSIBLE_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
enable_language(ASM)
enable_language(RC)
# CMake's default Windows libraries pull in DLLs no tier allows.
set(CMAKE_C_STANDARD_LIBRARIES "")
find_package(Python3 REQUIRED COMPONENTS Interpreter)

function(_impossible_compile target)
    set_target_properties(${target} PROPERTIES C_STANDARD 23 C_STANDARD_REQUIRED ON C_EXTENSIONS ON)
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:C>:-ffreestanding -fno-stack-protector -Wall -Wextra -Werror>
        "$<$<COMPILE_LANGUAGE:C>:-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.>" "$<$<COMPILE_LANGUAGE:C>:-ffile-prefix-map=${IMPOSSIBLE_SDK_DIR}=sdk>")
endfunction()

if(NOT TARGET ImpossibleSDK)
    add_library(ImpossibleSDK STATIC
        "${IMPOSSIBLE_SDK_DIR}/src/start.c" "${IMPOSSIBLE_SDK_DIR}/src/mem.c" "${IMPOSSIBLE_SDK_DIR}/src/chkstk.S"
        "${IMPOSSIBLE_SDK_DIR}/src/console.c" "${IMPOSSIBLE_SDK_DIR}/src/window.c")
    target_include_directories(ImpossibleSDK PUBLIC "${IMPOSSIBLE_SDK_DIR}/include")
    _impossible_compile(ImpossibleSDK)
    # These functions are the library routines the compiler would otherwise call.
    set_source_files_properties("${IMPOSSIBLE_SDK_DIR}/src/mem.c" PROPERTIES COMPILE_OPTIONS -fno-builtin)
endif()

if(NOT TARGET ImpossibleUI)
    file(GLOB _ui CONFIGURE_DEPENDS "${IMPOSSIBLE_SDK_DIR}/ui/*.c")
    add_library(ImpossibleUI STATIC ${_ui})
    target_include_directories(ImpossibleUI PUBLIC "${IMPOSSIBLE_SDK_DIR}/include" "${IMPOSSIBLE_SDK_DIR}/ui")
    _impossible_compile(ImpossibleUI)
    # As Impossible OS compiles it: floating point does not build.
    target_compile_options(ImpossibleUI PRIVATE -mno-mmx -mno-sse -mno-sse2 -mno-80387)
endif()

function(impossible_add_program name)
    cmake_parse_arguments(arg "WINDOW;CONSOLE;UNCHECKED" "TIER" "SOURCES" ${ARGN})
    if(arg_WINDOW)
        set(subsystem windows)
        list(APPEND arg_SOURCES "${IMPOSSIBLE_SDK_DIR}/src/app.rc")
        set_source_files_properties("${IMPOSSIBLE_SDK_DIR}/src/app.rc" PROPERTIES
            OBJECT_DEPENDS "${IMPOSSIBLE_SDK_DIR}/src/app.manifest")
    elseif(arg_CONSOLE)
        set(subsystem console)
    else()
        message(FATAL_ERROR "${name}: say WINDOW or CONSOLE")
    endif()
    add_executable(${name} ${arg_SOURCES})
    _impossible_compile(${name})
    target_link_options(${name} PRIVATE -nostdlib -Wl,--entry=impossible_start -Wl,--subsystem,${subsystem}
        -Wl,--gc-sections -Wl,--no-insert-timestamp -Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va -s)
    target_link_libraries(${name} PRIVATE ImpossibleUI ImpossibleSDK kernel32 user32 gdi32 shell32 winmm ws2_32)
    if(NOT arg_UNCHECKED)
        if(NOT DEFINED arg_TIER)
            message(FATAL_ERROR "${name}: say which TIER it needs")
        endif()
        add_custom_command(TARGET ${name} POST_BUILD VERBATIM
            COMMAND Python3::Interpreter "${IMPOSSIBLE_SDK_DIR}/tools/check_imports.py" "$<TARGET_FILE:${name}>" --tier ${arg_TIER})
        set_property(TARGET ${name} APPEND PROPERTY LINK_DEPENDS
            "${IMPOSSIBLE_SDK_DIR}/abi/tiers.json" "${IMPOSSIBLE_SDK_DIR}/tools/check_imports.py")
    endif()
endfunction()
