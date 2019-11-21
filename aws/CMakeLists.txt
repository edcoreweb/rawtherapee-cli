# Common source files for both CLI and non-CLI execautables
set(CLISOURCEFILES
    alignedmalloc.cc
    editcallbacks.cc
    main-cli.cc
    multilangmgr.cc
    options.cc
    paramsedited.cc
    pathutils.cc
    threadutils.cc
    )

set(CLICUSTOMSOURCEFILES
    alignedmalloc.cc
    editcallbacks.cc
    main-cli-custom.cc
    multilangmgr.cc
    options.cc
    paramsedited.cc
    pathutils.cc
    threadutils.cc
    )


include_directories(BEFORE "${CMAKE_CURRENT_BINARY_DIR}")

if(APPLE)
    find_package(MacIntegration REQUIRED)
    # At the time of writing CMake has no module finder for gtkmacintegration so here we have it hard-coded, if installed via macports it should be in /opt/local/...
    set(EXTRA_LIB_RTGUI ${MacIntegration_LIBRARIES})
    set(EXTRA_INCDIR ${EXTRA_INCDIR} ${MacIntegration_INCLUDE_DIRS})
endif()

if(WIN32)
    set(EXTRA_SRC_CLI myicon.rc)
    set(EXTRA_LIB_RTGUI winmm)
    include_directories(${EXTRA_INCDIR}
        ${GIOMM_INCLUDE_DIRS}
        ${GIO_INCLUDE_DIRS}
        ${GLIB2_INCLUDE_DIRS}
        ${GLIBMM_INCLUDE_DIRS}
        ${GTKMM_INCLUDE_DIRS}
        ${GTK_INCLUDE_DIRS}
        ${LENSFUN_INCLUDE_DIRS}
        ${RSVG_INCLUDE_DIRS}
        )
    link_directories(. "${PROJECT_SOURCE_DIR}/rtexif"
        ${EXTRA_LIBDIR}
        ${GIOMM_LIBRARY_DIRS}
        ${GIO_LIBRARY_DIRS}
        ${GLIB2_LIBRARY_DIRS}
        ${GLIBMM_LIBRARY_DIRS}
        ${GTKMM_LIBRARY_DIRS}
        ${GTK_LIBRARY_DIRS}
        ${LENSFUN_LIBRARY_DIRS}
        ${RSVG_LIBRARY_DIRS}
        )
else()
    include_directories(${EXTRA_INCDIR}
        ${CANBERRA-GTK_INCLUDE_DIRS}
        ${EXPAT_INCLUDE_DIRS}
        ${FFTW3F_LIBRARY_DIRS}
        ${GIOMM_INCLUDE_DIRS}
        ${GIO_INCLUDE_DIRS}
        ${GLIB2_INCLUDE_DIRS}
        ${GLIBMM_INCLUDE_DIRS}
        ${GOBJECT_INCLUDE_DIRS}
        ${GTHREAD_INCLUDE_DIRS}
        ${GTKMM_INCLUDE_DIRS}
        ${GTK_INCLUDE_DIRS}
        ${IPTCDATA_INCLUDE_DIRS}
        ${LCMS_INCLUDE_DIRS}
        ${LENSFUN_INCLUDE_DIRS}
        ${RSVG_INCLUDE_DIRS}
        )
    link_directories(${EXTRA_LIBDIR}
        ${CANBERRA-GTK_LIBRARY_DIRS}
        ${EXPAT_LIBRARY_DIRS}
        ${FFTW3F_LIBRARY_DIRS}
        ${GIOMM_LIBRARY_DIRS}
        ${GIO_LIBRARY_DIRS}
        ${GLIB2_LIBRARY_DIRS}
        ${GLIBMM_LIBRARY_DIRS}
        ${GOBJECT_LIBRARY_DIRS}
        ${GTHREAD_LIBRARY_DIRS}
        ${GTKMM_LIBRARY_DIRS}
        ${GTK_LIBRARY_DIRS}
        ${IPTCDATA_LIBRARY_DIRS}
        ${LCMS_LIBRARY_DIRS}
        ${LENSFUN_LIBRARY_DIRS}
        ${RSVG_LIBRARY_DIRS}
        )
endif()

# Excluding libatomic needed by Clang/FreeBSD, #3636
if(OPENMP_FOUND AND NOT APPLE AND NOT "${CMAKE_SYSTEM_NAME}" STREQUAL "FreeBSD")
    set(EXTRA_LIB_RTGUI ${EXTRA_LIB_RTGUI} "atomic")
endif()

# Create config.h which defines where data are stored
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/config.h.in" "${CMAKE_CURRENT_BINARY_DIR}/config.h")

# Create new executables targets
add_executable(rth-cli ${EXTRA_SRC_CLI} ${CLISOURCEFILES})
add_executable(rth-cli-custom ${EXTRA_SRC_CLI} ${CLICUSTOMSOURCEFILES})

# Add dependencies to executables targets
add_dependencies(rth-cli UpdateInfo)
add_dependencies(rth-cli-custom UpdateInfo)

#Define a target specific definition to use in code
target_compile_definitions(rth-cli PUBLIC CLIVERSION)
target_compile_definitions(rth-cli-custom PUBLIC CLIVERSION)

# Set executables targets properties, i.e. output filename and compile flags
set_target_properties(rth-cli PROPERTIES COMPILE_FLAGS "${CMAKE_CXX_FLAGS}" OUTPUT_NAME rawtherapee-cli)
set_target_properties(rth-cli-custom PROPERTIES COMPILE_FLAGS "${CMAKE_CXX_FLAGS}" OUTPUT_NAME rawtherapee-cli-custom)

# Add linked libraries dependencies to executables targets
target_link_libraries(rth-cli rtengine
    ${CAIROMM_LIBRARIES}
    ${EXPAT_LIBRARIES}
    ${EXTRA_LIB_RTGUI}
    ${FFTW3F_LIBRARIES}
    ${GIOMM_LIBRARIES}
    ${GIO_LIBRARIES}
    ${GLIB2_LIBRARIES}
    ${GLIBMM_LIBRARIES}
    ${GOBJECT_LIBRARIES}
    ${GTHREAD_LIBRARIES}
    ${IPTCDATA_LIBRARIES}
    ${JPEG_LIBRARIES}
    ${LCMS_LIBRARIES}
    ${PNG_LIBRARIES}
    ${TIFF_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${LENSFUN_LIBRARIES}
    ${RSVG_LIBRARIES}
    )

# Add linked libraries dependencies to executables targets
target_link_libraries(rth-cli-custom rtengine
    ${CAIROMM_LIBRARIES}
    ${EXPAT_LIBRARIES}
    ${EXTRA_LIB_RTGUI}
    ${FFTW3F_LIBRARIES}
    ${GIOMM_LIBRARIES}
    ${GIO_LIBRARIES}
    ${GLIB2_LIBRARIES}
    ${GLIBMM_LIBRARIES}
    ${GOBJECT_LIBRARIES}
    ${GTHREAD_LIBRARIES}
    ${IPTCDATA_LIBRARIES}
    ${JPEG_LIBRARIES}
    ${LCMS_LIBRARIES}
    ${PNG_LIBRARIES}
    ${TIFF_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${LENSFUN_LIBRARIES}
    ${RSVG_LIBRARIES}
    )

# Install executables
install(TARGETS rth-cli DESTINATION ${BINDIR})
install(TARGETS rth-cli-custom DESTINATION ${BINDIR})
