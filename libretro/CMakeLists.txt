
add_library(pcsx2_libretro SHARED)

#target_link_libraries(PCSX2 PRIVATE
#	${wxWidgets_LIBRARIES}
#	${AIO_LIBRARIES}
#	${GLIB_LIBRARIES}
#	${GLIB_GIO_LIBRARIES}
#)

#add_link_options(-fuse-ld=gold)
#add_link_options(-Wl,--gc-sections,--print-symbol-counts,sym.log)

target_sources(pcsx2_libretro PRIVATE
   ${CMAKE_SOURCE_DIR}/libretro/main.cpp
   ${CMAKE_SOURCE_DIR}/libretro/options.cpp
   ${CMAKE_SOURCE_DIR}/libretro/input.cpp
   ${CMAKE_SOURCE_DIR}/libretro/Console.cpp
   ${CMAKE_SOURCE_DIR}/libretro/SPU2.cpp
   ${CMAKE_SOURCE_DIR}/libretro/DEV9.cpp
   ${CMAKE_SOURCE_DIR}/libretro/USB.cpp
   ${CMAKE_SOURCE_DIR}/libretro/InputRecording.cpp

   ${CMAKE_SOURCE_DIR}/common/GL/ContextRetroGL.cpp
#   ${CMAKE_SOURCE_DIR}/pcsx2/USB/USBNull.cpp
#   ${pcsx2LTOSources}
#   ${pcsx2GuiSources}
)

target_link_libraries(pcsx2_libretro PRIVATE
	PCSX2_FLAGS
	PCSX2
)

target_include_directories(PCSX2_FLAGS INTERFACE
	"${CMAKE_SOURCE_DIR}/libretro"
)

target_include_directories(pcsx2_libretro PRIVATE
	"${CMAKE_SOURCE_DIR}"
	"${CMAKE_SOURCE_DIR}/libretro"
	"${CMAKE_SOURCE_DIR}/common/include"
	"${CMAKE_SOURCE_DIR}/3rdparty/include"
	"${CMAKE_SOURCE_DIR}/pcsx2"
)

#include_directories(. ${CMAKE_SOURCE_DIR}/libretro ${CMAKE_SOURCE_DIR}/common)
set_target_properties(pcsx2_libretro PROPERTIES
   LIBRARY_OUTPUT_NAME pcsx2_libretro
   PREFIX ""
)

#   set(LIBRARY_OUTPUT_PATH "${CMAKE_BINARY_DIR}")

if(CMAKE_C_COMPILER_ID MATCHES "Clang")
   set(CLANG 1)
endif()

if(NOT MSVC AND NOT CLANG)
   set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined")
endif()

if(PACKAGE_MODE)
    install(TARGETS pcsx2_libretro DESTINATION ${BIN_DIR})
else(PACKAGE_MODE)
    install(TARGETS pcsx2_libretro DESTINATION ${CMAKE_SOURCE_DIR}/bin)
endif(PACKAGE_MODE)

#setup_main_executable(pcsx2-libretro)