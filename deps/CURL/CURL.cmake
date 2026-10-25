set(_curl_platform_flags 
  -DENABLE_IPV6:BOOL=ON
  -DENABLE_VERSIONED_SYMBOLS:BOOL=ON
  -DENABLE_THREADED_RESOLVER:BOOL=ON
  -DENABLE_MANUAL:BOOL=OFF
  -DCURL_DISABLE_LDAP:BOOL=ON
  -DCURL_DISABLE_LDAPS:BOOL=ON
  -DCURL_DISABLE_RTSP:BOOL=ON
  -DCURL_DISABLE_DICT:BOOL=ON
  -DCURL_DISABLE_TELNET:BOOL=ON
  -DCURL_DISABLE_POP3:BOOL=ON
  -DCURL_DISABLE_IMAP:BOOL=ON
  -DCURL_DISABLE_SMB:BOOL=ON
  -DCURL_DISABLE_SMTP:BOOL=OFF
  -DCURL_DISABLE_GOPHER:BOOL=ON
  -DCURL_DISABLE_TFTP:BOOL=ON
  -DCURL_DISABLE_MQTT:BOOL=ON
  #-DHTTP_ONLY=ON

  -DCURL_USE_GSSAPI:BOOL=OFF
  -DCURL_USE_LIBSSH2:BOOL=OFF
  -DCURL_USE_LIBSSH:BOOL=OFF
  -DCURL_USE_LIBPSL:BOOL=OFF
  -DUSE_LIBIDN2:BOOL=OFF
  -DCURL_BROTLI:BOOL=OFF
  -DCURL_ZSTD:BOOL=OFF
  -DBUILD_LIBCURL_DOCS:BOOL=OFF
  -DBUILD_MISC_DOCS:BOOL=OFF
  -DENABLE_CURL_MANUAL:BOOL=OFF
  -DUSE_RTMP:BOOL=OFF
  -DUSE_NGHTTP2:BOOL=OFF
  -DCURL_USE_MBEDTLS:BOOL=OFF
)

if (WIN32)
  #set(_curl_platform_flags  ${_curl_platform_flags} -DCMAKE_USE_SCHANNEL=ON)
  set(_curl_platform_flags  ${_curl_platform_flags} -DCURL_USE_OPENSSL=ON -DCURL_CA_PATH:STRING=none)
elseif (APPLE)
  set(_curl_platform_flags 
    
    ${_curl_platform_flags}

    #-DCURL_USE_SECTRANSP:BOOL=ON 
    -DCURL_USE_OPENSSL:BOOL=ON

    -DCURL_CA_PATH:STRING=none
  )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(_curl_platform_flags 

    ${_curl_platform_flags}

    -DCURL_USE_OPENSSL:BOOL=ON

    -DCURL_CA_PATH:STRING=none
    -DCURL_CA_BUNDLE:STRING=none
    -DCURL_CA_FALLBACK:BOOL=ON
  )
endif ()

if (BUILD_SHARED_LIBS)
  set(_curl_static OFF)
else()
  set(_curl_static ON)
endif()

orcaslicer_add_cmake_project(CURL
  # GIT_REPOSITORY      https://github.com/curl/curl.git
  # GIT_TAG             curl-7_75_0
  URL                 https://curl.se/download/curl-8.23.0.tar.xz
  URL_HASH            SHA256=8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5
  DEPENDS             ${ZLIB_PKG}
  # PATCH_COMMAND       ${GIT_EXECUTABLE} checkout -f -- . && git clean -df && 
  #                     ${GIT_EXECUTABLE} apply --whitespace=fix ${CMAKE_CURRENT_LIST_DIR}/curl-mods.patch
  CMAKE_ARGS
    -DBUILD_TESTING:BOOL=OFF
    -DBUILD_CURL_EXE:BOOL=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCURL_STATICLIB=${_curl_static}
    -DBUILD_STATIC_LIBS=${_curl_static}
    -DBUILD_SHARED_LIBS=OFF
    ${_curl_platform_flags}
)

if(NOT OPENSSL_FOUND)
  # (openssl may or may not be built)
  add_dependencies(dep_CURL ${OPENSSL_PKG})
endif()

if (MSVC)
    add_debug_dep(dep_CURL)
endif ()
