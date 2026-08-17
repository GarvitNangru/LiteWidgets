cmake_minimum_required(VERSION 3.20)
project(LiteWidgets C)

set(CMAKE_C_STANDARD 17)

# Output binaries to bin directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Setup executable
add_executable(LiteWidgets WIN32
    src/main.c
    src/desktop.c
    src/widget.c
    src/config.c
    src/widgets/clock.c
    src/widgets/image.c
    src/widgets/notes.c
)

# Include directories
target_include_directories(LiteWidgets PRIVATE src)

# Link Windows libraries
target_link_libraries(LiteWidgets PRIVATE
    user32
    gdi32
    gdiplus
    shell32
    shcore
)

# Embed manifest for DPI awareness
target_sources(LiteWidgets PRIVATE app.manifest)
