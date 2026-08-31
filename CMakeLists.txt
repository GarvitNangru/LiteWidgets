cmake_minimum_required(VERSION 3.20)
project(LiteWidgets C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(LiteWidgets WIN32
    src/main.c
    src/desktop.c
    src/widget.c
    src/config.c
    src/spec.c
    src/style.c
    src/drawing.c
    src/layout.c
    src/timefmt.c
    src/autostart.c
    src/settings.c
    src/widgets/clock.c
    src/widgets/image.c
    src/widgets/notes.c
    app.manifest
)

target_include_directories(LiteWidgets PRIVATE src)

target_compile_definitions(LiteWidgets PRIVATE
    _CRT_SECURE_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(LiteWidgets PRIVATE /W4)
    # Statically link the CRT so the binary runs without a redistributable.
    set_property(TARGET LiteWidgets PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

target_link_libraries(LiteWidgets PRIVATE
    user32 gdi32 gdiplus shell32 shcore
    advapi32 shlwapi comdlg32 comctl32 msimg32
)
