# - Find SFML library (v2.x)
# This module defines:
#   SFML_FOUND        - If false, don't try to use SFML.
#   SFML_INCLUDE_DIR  - Directory containing SFML headers.
#   SFML_LIBRARIES    - Full list of required SFML libraries.
#   SFML_<COMPONENT>_LIBRARY - Component-specific libraries

set(FIND_SFML_PATHS
    ${SFML_ROOT}
    $ENV{SFML_ROOT}
    "C:/SFML"
)

# Locate headers
find_path(SFML_INCLUDE_DIR SFML/Config.hpp
    PATH_SUFFIXES include
    PATHS ${FIND_SFML_PATHS}
)

# Function to locate SFML libraries for each component
function(find_sfml_component COMPONENT LIB_NAME)
    find_library(SFML_${COMPONENT}_LIBRARY_RELEASE
        NAMES ${LIB_NAME}
        PATH_SUFFIXES lib
        PATHS ${FIND_SFML_PATHS}
    )
    find_library(SFML_${COMPONENT}_LIBRARY_DEBUG
        NAMES ${LIB_NAME}-d
        PATH_SUFFIXES lib
        PATHS ${FIND_SFML_PATHS}
    )

    if(SFML_${COMPONENT}_LIBRARY_RELEASE)
        set(SFML_${COMPONENT}_LIBRARY optimized ${SFML_${COMPONENT}_LIBRARY_RELEASE} CACHE STRING "")
    endif()

    if(SFML_${COMPONENT}_LIBRARY_DEBUG)
        set(SFML_${COMPONENT}_LIBRARY ${SFML_${COMPONENT}_LIBRARY} debug ${SFML_${COMPONENT}_LIBRARY_DEBUG} CACHE STRING "")
    endif()

    set(SFML_${COMPONENT}_LIBRARY ${SFML_${COMPONENT}_LIBRARY} PARENT_SCOPE)
    list(APPEND SFML_LIBRARIES ${SFML_${COMPONENT}_LIBRARY})
    set(SFML_LIBRARIES ${SFML_LIBRARIES} PARENT_SCOPE)
endfunction()

# Required components (audio, graphics, etc.)
foreach(comp IN LISTS SFML_FIND_COMPONENTS)
    string(TOUPPER ${comp} UPPERCOMP)
    find_sfml_component(${UPPERCOMP} sfml-${comp})
endforeach()

# Handle package status
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SFML
    REQUIRED_VARS SFML_INCLUDE_DIR SFML_LIBRARIES
)

# Export variables
if(SFML_FOUND)
    set(SFML_INCLUDE_DIRS ${SFML_INCLUDE_DIR})
endif()
