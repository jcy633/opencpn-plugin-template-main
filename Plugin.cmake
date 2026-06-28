# -------- Options ----------

set(OCPN_TEST_REPO
    "opencpn/ncdf-alpha"
    CACHE STRING "Default repository for untagged builds"
)
set(OCPN_BETA_REPO
    "opencpn/ncdf-beta"
    CACHE STRING "Default repository for tagged builds matching 'beta'"
)
set(OCPN_RELEASE_REPO
    "opencpn/ncdf-prod"
    CACHE STRING "Default repository for tagged builds not matching 'beta'"
)

# -------  Plugin setup --------

set(PKG_NAME ncdf_pi)
set(PKG_VERSION  0.5.0.0)
set(PKG_PRERELEASE "")

set(DISPLAY_NAME [ncdf])
set(PLUGIN_API_NAME [ncdf])
set(PKG_SUMMARY "ncdf PlugIn for OpenCPN")
set(PKG_DESCRIPTION [=[
"ncdf PlugIn for OpenCPN\n\
For surface current direction and speed."
]=])

set(PKG_AUTHOR "David Register, Mike Rossiter")
set(PKG_IS_OPEN_SOURCE "yes")
set(CPACK_PACKAGE_HOMEPAGE_URL https://github.com/opencpn-plugins/ncdf_pi)
set(PKG_INFO_URL https://opencpn.org/OpenCPN/plugins/)

# ----------------------------------------------------------------------------------------------------------------#

set(SRC
    src/ncdf_pi.cpp
    src/gui.cpp
    src/ncdf_reader.cpp
    src/ncdfoverlayfactory.cpp
    src/ncdfdata.cpp
    src/ncdf.cpp
    src/IsoLine2.cpp
    src/icons2.cpp
    src/helper.cpp
)

set(HEADERS
    include/ncdf_pi.h
    include/gui.h
    include/ncdf_reader.h
    include/ncdfoverlayfactory.h
    include/ncdfdata.h
    include/ncdf.h
    include/IsoLine2.h
    include/icons2.h
    include/helper.h
)

source_group("" FILES
    ${SRC}
    ${HEADERS}
)

add_definitions("-DocpnUSE_GL")

set(PKG_API_LIB api-19)

macro(late_init)
endmacro()

macro(add_plugin_libraries)
    # netCDF libraries are linked in CMakeLists.txt directly
    
    add_subdirectory("${CMAKE_SOURCE_DIR}/opencpn-libs/plugin_dc")
    target_link_libraries(${PACKAGE_NAME} ocpn::plugin-dc)

    add_subdirectory("${CMAKE_SOURCE_DIR}/opencpn-libs/wxJSON")
    target_link_libraries(${PACKAGE_NAME} ocpn::wxjson)
endmacro()