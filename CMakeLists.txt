cmake_minimum_required(VERSION 3.10)

project(RaceCarGame)

set(CMAKE_CXX_STANDARD 17)

# Path to SFML
set(SFML_DIR "C:/SFML/lib/cmake/SFML")

# Add the main executable
add_executable(RaceCarGame
    RaceCarGame/src/main.cpp
)

# Include headers if needed
target_include_directories(RaceCarGame PRIVATE RaceCarGame)

# Find SFML
find_package(SFML 2.5 COMPONENTS graphics window system audio REQUIRED)

# Link SFML libraries
target_link_libraries(RaceCarGame sfml-graphics sfml-window sfml-system sfml-audio)

# Set working directory for resources
set(RESOURCE_DIRS fonts images sounds)

# Copy each resource folder after build
foreach(dir ${RESOURCE_DIRS})
    add_custom_command(TARGET RaceCarGame POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/RaceCarGame/${dir}"
        "$<TARGET_FILE_DIR:RaceCarGame>/${dir}"
    )
endforeach()
