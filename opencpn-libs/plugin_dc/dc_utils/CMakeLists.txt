# ~~~
# Summary:      Plugin OpenGL support library cmake setup
# License:      GPLv3+
# Copyright (c) 2021 Alec Leamas
# ~~~

cmake_minimum_required(VERSION 3.10)

if (TARGET ocpn::dc-utils)
  return ()
endif ()

# Set C++ standard - must be done before adding library
# Force C++14 for MSVC to avoid C++17 cmath issues
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ensure CMAKE_CXX_STANDARD is applied to all targets
set(CMAKE_CXX_STANDARD 14 CACHE INTERNAL "C++ standard" FORCE)

set(SRC
  src/pi_shaders.cpp
  src/pidc.cpp
  src/qtstylesheet.cpp
  src/TexFont.cpp
  include/linmath.h
  include/pi_shaders.h
  include/pidc.h
  include/qtstylesheet.h
  include/TexFont.h
)

add_library(_DC_UTILS STATIC ${SRC})
add_library(ocpn::dc-utils ALIAS _DC_UTILS)

# Force C++14 for this target
set_target_properties(_DC_UTILS PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  set_property(TARGET _DC_UTILS PROPERTY COMPILE_FLAGS "-fvisibility=hidden")
endif ()
set_target_properties(_DC_UTILS PROPERTIES POSITION_INDEPENDENT_CODE True)

if (PLUGINDC_USE_GL)
  target_compile_definitions(_DC_UTILS PRIVATE ocpnUSE_GL)
endif ()

# Use wxWidgets as DLL
if (WIN32)
  target_compile_definitions(_DC_UTILS PRIVATE WXUSINGDLL _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
endif ()

if (WIN32)
  if (TARGET windows::headers)
    target_link_libraries(_DC_UTILS PRIVATE windows::headers)
  else ()
    message(WARNING "Required windows::headers target missing")
  endif ()
endif ()

target_link_libraries(_DC_UTILS PRIVATE ocpn::api)
target_link_libraries(_DC_UTILS PRIVATE ocpn::pluginglu)

if (QT_ANDROID)
  target_include_directories(_DC_UTILS PUBLIC include/android)
endif ()

target_include_directories(
  _DC_UTILS PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
