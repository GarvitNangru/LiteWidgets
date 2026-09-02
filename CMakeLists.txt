cmake_minimum_required(VERSION 3.20)
project(LiteWidgets C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# The application icon is drawn from source at build time rather than checked
# in, so the repository stays free of binary assets. assets/icon.svg is the
# same design for the web; tools/mkicon.c is the one the build consumes.
add_executable(mkicon tools/mkicon.c)
target_include_directories(mkicon PRIVATE src)
target_compile_definitions(mkicon PRIVATE _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN)
target_link_libraries(mkicon PRIVATE gdiplus user32)

set(APP_ICON ${CMAKE_CURRENT_BINARY_DIR}/app.ico)
add_custom_command(OUTPUT ${APP_ICON}
    COMMAND mkicon ${APP_ICON}
    DEPENDS mkicon
    COMMENT "Drawing the application icon")

# rc resolves "app.ico" through its include path, which is where it lands.
set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /I \"${CMAKE_CURRENT_BINARY_DIR}\" /I \"${CMAKE_CURRENT_SOURCE_DIR}/src\"")
set_source_files_properties(app.icon.rc PROPERTIES OBJECT_DEPENDS ${APP_ICON})

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
    src/widgets/gauge.c
    src/widgets/calendar.c
    app.manifest
    app.icon.rc
)

add_dependencies(LiteWidgets mkicon)

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
