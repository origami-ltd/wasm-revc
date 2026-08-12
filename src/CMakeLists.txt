if(NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
endif()

file(GLOB_RECURSE ${PROJECT}_SOURCES "*.cpp" "*.h" "*.rc")

function(header_directories RETURN_LIST)
    file(GLOB_RECURSE ALL_SRCS *.h *.cpp *.c)
    set(RELDIRS)
    foreach(SRC ${ALL_SRCS})
        file(RELATIVE_PATH RELSRC "${CMAKE_CURRENT_SOURCE_DIR}" "${SRC}")
        get_filename_component(RELDIR "${RELSRC}" DIRECTORY)
        list(APPEND RELDIRS ${RELDIR})
    endforeach()
    list(REMOVE_DUPLICATES RELDIRS)
    set(${RETURN_LIST} ${RELDIRS} PARENT_SCOPE)
endfunction()

header_directories(${PROJECT}_INCLUDES)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/extras/GitSHA1.cpp.in" "${CMAKE_CURRENT_BINARY_DIR}/extras/GitSHA1.cpp" @ONLY)
list(APPEND ${PROJECT}_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/extras/GitSHA1.cpp")

if(NOT ANDROID)
    add_executable(${EXECUTABLE} WIN32 ${${PROJECT}_SOURCES})
elseif(ANDROID)
    add_library(${EXECUTABLE} SHARED ${${PROJECT}_SOURCES})
endif()

target_link_libraries(${EXECUTABLE} PRIVATE
    librw::librw
)
if(NOT EMSCRIPTEN)
    target_link_libraries(${EXECUTABLE} PRIVATE Threads::Threads)
endif()

if(ANDROID)
    target_link_libraries(${EXECUTABLE} PRIVATE
            log
            GLESv3
            OpenSLES
    )
endif()

if(EMSCRIPTEN)
    # AL/efx.h shim — emscripten's OpenAL has no effects extension. See the header.
    target_include_directories(${EXECUTABLE} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/audio/oal/emscripten")
endif()

target_include_directories(${EXECUTABLE}
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${${PROJECT}_INCLUDES}>
)

target_compile_definitions(${EXECUTABLE}
    PRIVATE
        $<IF:$<CONFIG:DEBUG>,DEBUG,NDEBUG>
        LIBRW
        CMAKE_NO_AUTOLINK
)

if(LIBRW_PLATFORM_D3D9)
    target_compile_definitions(${EXECUTABLE}
        PUBLIC
            USE_D3D9
    )
endif()

if(ANDROID)
    target_compile_definitions(${EXECUTABLE} PRIVATE
            ANDROID
            _ANDROID
            LINUX
            _LINUX
            POSIX
            _POSIX
            NO_HOOK_MALLOC
            AL_LIBTYPE_STATIC
            GNUC
            RW_GL3
            LIBRW_SDL2
            LIBRW
            AUDIO_OAL
            USE_OUR_VERSIONING
            HAVE_EFFICIENT_UNALIGNED_ACCESS
            VECTORIZE_SINCOS
        )
else()
    target_compile_definitions(${EXECUTABLE} PRIVATE CMAKE_BUILD)
    target_compile_definitions(${EXECUTABLE} PRIVATE USE_OUR_VERSIONING)
endif()

if(${PROJECT}_AUDIO STREQUAL "OAL" AND EMSCRIPTEN)
    # OpenAL is part of emscripten's JS runtime and mpg123 is one of its ports; both are flags,
    # not packages, so there is nothing for find_package to locate.
    target_link_options(${EXECUTABLE} PRIVATE "-lopenal")
    target_compile_definitions(${EXECUTABLE} PRIVATE AUDIO_OAL)
elseif(${PROJECT}_AUDIO STREQUAL "OAL")
    find_package(OpenAL REQUIRED)
    if(TARGET OpenAL::OpenAL)
        target_link_libraries(${EXECUTABLE} PRIVATE OpenAL::OpenAL)
    else()
        target_include_directories(${EXECUTABLE} PRIVATE ${OPENAL_INCLUDE_DIR})
        target_link_libraries(${EXECUTABLE} PRIVATE ${OPENAL_LIBRARY})
        target_compile_definitions(${EXECUTABLE} PRIVATE ${OPENAL_DEFINITIONS})
    endif()
    target_compile_definitions(${EXECUTABLE} PRIVATE AUDIO_OAL)
elseif(${PROJECT}_AUDIO STREQUAL "MSS")
    find_package(MilesSDK REQUIRED)
    target_compile_definitions(${EXECUTABLE} PRIVATE AUDIO_MSS)
    target_link_libraries(${EXECUTABLE} PRIVATE MilesSDK::MilesSDK)
endif()

if(EMSCRIPTEN)
    target_compile_options(${EXECUTABLE} PRIVATE "-sUSE_MPG123")
    target_link_options(${EXECUTABLE} PRIVATE "-sUSE_MPG123")
else()
    find_package(mpg123 REQUIRED)
    target_link_libraries(${EXECUTABLE} PRIVATE
        MPG123::libmpg123
    )
endif()
if(${PROJECT}_WITH_OPUS)
    find_package(opusfile REQUIRED)
    target_link_libraries(${EXECUTABLE} PRIVATE
        opusfile::opusfile
    )
    target_compile_definitions(${EXECUTABLE} PRIVATE AUDIO_OPUS)
endif()
if(${PROJECT}_WITH_LIBSNDFILE)
    find_package(SndFile REQUIRED)
    target_link_libraries(${EXECUTABLE} PRIVATE
        SndFile::SndFile
    )
    target_compile_definitions(${EXECUTABLE} PRIVATE AUDIO_OAL_USE_SNDFILE)
endif()

target_compile_definitions(${EXECUTABLE} PRIVATE )

option(${PROJECT}_WITH_SANITIZERS "Use UB sanitizers (better crash log)" OFF)
option(${PROJECT}_WITH_ASAN "Use Address sanitizer (better crash log)" OFF)
option(${PROJECT}_DISABLE_WARNINGS "Disable build warnings" ON)

if(${PROJECT}_DISABLE_WARNINGS)
    target_compile_options(${EXECUTABLE} PRIVATE
            -w)
endif()

if(${PROJECT}_WITH_SANITIZERS)
    target_compile_options(${EXECUTABLE} PUBLIC
            -fsanitize=undefined,float-divide-by-zero,integer,implicit-conversion,implicit-integer-truncation,implicit-integer-arithmetic-value-change,local-bounds,nullability
            -g3 -fno-omit-frame-pointer)
    target_link_options(${EXECUTABLE} PUBLIC -fsanitize=undefined,float-divide-by-zero,integer,implicit-conversion,implicit-integer-truncation,implicit-integer-arithmetic-value-change,local-bounds,nullability)
endif()

if(${PROJECT}_WITH_ASAN)
    target_compile_options(${EXECUTABLE} PUBLIC -fsanitize=address -g3 -fno-omit-frame-pointer)
    target_link_options(${EXECUTABLE} PUBLIC -fsanitize=address)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    if(ANDROID)
        target_compile_options(${EXECUTABLE}
            PRIVATE
                -pthread
                -std=c++11
                -fno-strict-aliasing
                -fexceptions
                -Wcast-align
                -Wuninitialized
                -Winit-self
                -Wstrict-aliasing
                -Wno-reorder
                -Wno-unknown-pragmas
                -Wno-unused-function
                -Wno-unused-but-set-variable
                -Wno-unused-value
                -Wno-unused-command-line-argument
                -faligned-new
        )
    endif()
    target_compile_options(${EXECUTABLE}
        PRIVATE
            "-Wall"
    )
    if (NOT LIBRW_PLATFORM_PS2)
        target_compile_options(${EXECUTABLE}
            PRIVATE
                -Wextra
                -Wdouble-promotion
                -Wpedantic
        )
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(${EXECUTABLE}
        PUBLIC
            /Zc:sizedDealloc-
    )
endif()

if(NINTENDO_SWITCH)
    set(${PROJECT}_C_CXX_EXTENSIONS ON)
else()
    set(${PROJECT}_C_CXX_EXTENSIONS OFF)
endif()

if(LIBRW_PLATFORM_GL3 AND LIBRW_GL3_GFXLIB STREQUAL "GLFW")
	include(CheckSymbolExists)

	set(CMAKE_REQUIRED_LIBRARIES glfw)
	set(CMAKE_REQUIRED_DEFINITIONS -DGLFW_EXPOSE_NATIVE_X11)
	check_symbol_exists(glfwGetX11Display "GLFW/glfw3.h;GLFW/glfw3native.h" GLFW_HAS_X11)
	unset(CMAKE_REQUIRED_DEFINITIONS)
	unset(CMAKE_REQUIRED_LIBRARIES)

	if (GLFW_HAS_X11)
		find_package(X11 REQUIRED)
		target_link_libraries(${EXECUTABLE} PRIVATE X11::X11)
		target_compile_definitions(${EXECUTABLE} PRIVATE GET_KEYBOARD_INPUT_FROM_X11)
	endif (GLFW_HAS_X11)
endif()

set_target_properties(${EXECUTABLE}
    PROPERTIES
        C_STANDARD 11
        C_EXTENSIONS ${${PROJECT}_C_CXX_EXTENSIONS}
        C_STANDARD_REQUIRED ON
        CXX_STANDARD 11
        CXX_EXTENSIONS ${${PROJECT}_C_CXX_EXTENSIONS}
        CXX_STANDARD_REQUIRED ON
)

if(${PROJECT}_INSTALL)
    install(
        TARGETS ${EXECUTABLE}
        EXPORT ${EXECUTABLE}-targets
        RUNTIME DESTINATION "."
    )
    if(MSVC)
        install(FILES $<TARGET_PDB_FILE:${EXECUTABLE}> DESTINATION "." OPTIONAL)
    endif()
endif()

reVC_platform_target(${EXECUTABLE} INSTALL)

if(EMSCRIPTEN)
    # The browser build. Notes on the non-obvious ones:
    #   MODULARIZE/EXPORT_ES6 — the shell does `import("/reVC.js")` and calls the default export,
    #     the same shape the Generals port uses.
    #   ASYNCIFY — the engine keeps its blocking game loop and yields once per frame; see the
    #     emscripten_sleep in skel/sdl2/sdl2.cpp.
    #   Fixed 1 GiB heap, no growth — growing the heap detaches the typed-array views WebGL and
    #     the audio path hold, and this 2002 codebase casts pointers to signed ints anyway.
    #   FULL_ES3 + WebGL 2 — librw's gl3 backend runs as GLES 3 here (RW_GLES3).
    set_target_properties(${EXECUTABLE} PROPERTIES SUFFIX ".js")
    target_link_options(${EXECUTABLE} PRIVATE
        "-sMODULARIZE=1"
        "-sEXPORT_ES6=1"
        "-sEXPORT_NAME=createReVC"
        "-sENVIRONMENT=web,worker"
        "-sINITIAL_MEMORY=1073741824"
        "-sALLOW_MEMORY_GROWTH=0"
        "-sSTACK_SIZE=5242880"
        "-sMIN_WEBGL_VERSION=2"
        "-sMAX_WEBGL_VERSION=2"
        "-sFULL_ES3=1"
        "-sGL_ENABLE_GET_PROC_ADDRESS=1"
        "-sASYNCIFY=1"
        "-sASYNCIFY_STACK_SIZE=131072"
        "-sFORCE_FILESYSTEM=1"
        "-sEXIT_RUNTIME=0"
        "-sEXPORTED_RUNTIME_METHODS=FS,IDBFS,callMain,ccall,cwrap,addRunDependency,removeRunDependency"
        "-sEXPORTED_FUNCTIONS=_main,_malloc,_free,_ViceLogicFrame,_ViceScreenWidth,_ViceScreenHeight,_ViceGameState,_ViceMenuActive,_ViceMenuScreen,_ViceForeground,_ViceIconified,_ViceMouseX,_ViceMouseY,_ViceWindowW,_ViceWindowH,_ViceFrontendStage,_ViceMenuFade,_ViceFirstStart,_ViceTimeMs,_ViceLogicalFrames"
        "-lidbfs.js"
    )
    # REVC_WEB_DEBUG=1 turns the useless "Aborted()" into the actual assertion text.
    if(DEFINED ENV{REVC_WEB_DEBUG})
        target_link_options(${EXECUTABLE} PRIVATE "-sASSERTIONS=2" "-sSTACK_OVERFLOW_CHECK=2" "-g2")
    endif()
endif()
