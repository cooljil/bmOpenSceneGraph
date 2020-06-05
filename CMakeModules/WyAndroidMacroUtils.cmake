MACRO(WY_ANDROID_3RD_PARTY 3RD_PARTY_PATH)
#------JPEG---------------------------------------------------
FIND_PATH(JPEG_INCLUDE_DIR Android.mk
    ${3RD_PARTY_PATH}/3rdparty/libjpeg NO_CMAKE_FIND_ROOT_PATH
)
if(JPEG_INCLUDE_DIR)
    message(STATUS "Jpeg found ${JPEG_INCLUDE_DIR}" )
    set(JPEG_FOUND "Yes")
#    install(DIRECTORY 3rdparty/build/libjpeg/ DESTINATION ./ )
else(JPEG_INCLUDE_DIR)
    message(STATUS "Jpeg missing" )
endif()

#------PNG----------------------------------------------------
FIND_PATH(PNG_INCLUDE_DIR Android.mk
    ${3RD_PARTY_PATH}/3rdparty/libpng NO_CMAKE_FIND_ROOT_PATH
)
if(PNG_INCLUDE_DIR)
    message(STATUS "PNG found ${PNG_INCLUDE_DIR}" )
    set(PNG_FOUND "Yes")
else(PNG_INCLUDE_DIR)
    message(STATUS "PNG missing" )
endif()

#------GIF-----------------------------------------------------
FIND_PATH(GIFLIB_INCLUDE_DIR Android.mk
    ${3RD_PARTY_PATH}/3rdparty/giflib NO_CMAKE_FIND_ROOT_PATH
)
if(GIFLIB_INCLUDE_DIR)
    message(STATUS "GIF found ${GIFLIB_INCLUDE_DIR}" )
    set(GIFLIB_FOUND "Yes")
else(GIFLIB_INCLUDE_DIR)
    message(STATUS "GIF missing" )
endif()

#------TIF------------------------------------------------------
FIND_PATH(TIFF_INCLUDE_DIR Android.mk
    ${3RD_PARTY_PATH}/3rdparty/libtiff NO_CMAKE_FIND_ROOT_PATH
)
if(TIFF_INCLUDE_DIR)
    message(STATUS "TIF found ${TIFF_INCLUDE_DIR}" )
    set(TIFF_FOUND "Yes")
else(TIFF_INCLUDE_DIR)
    message(STATUS "TIF missing" )
endif()

#------ZLIB----------------------------------------------------
#FIND_PATH(ZLIB_INCLUDE_DIR Android.mk
#    ${3RD_PARTY_PATH}/3rdparty/zlib
#)
#set(ENV{AND_OSG_LIB_NAMES} "$ENV{AND_OSG_LIB_NAMES} zlib")
#set(ENV{AND_OSG_LIB_PATHS} "$ENV{AND_OSG_LIB_PATHS}include ${ZLIB_INCLUDE_DIR}/Android.mk \n")
#if(ZLIB_INCLUDE_DIR)
#    message(STATUS "ZLIB found ${ZLIB_INCLUDE_DIR}" )
#    set(ZLIB_FOUND "Yes")
#else(ZLIB_INCLUDE_DIR)
#    message(STATUS "ZLIB missing" )
#endif()


#-------CURL------------------------------------
FIND_PATH(CURL_DIR Android.mk
    ${3RD_PARTY_PATH}/3rdparty/curl NO_CMAKE_FIND_ROOT_PATH
)
set(CURL_INCLUDE_DIR ${CURL_DIR}/include)
set(CURL_INCLUDE_DIRS ${CURL_DIR}/include) #Both are defined in FindCurl
if(CURL_DIR)
    message(STATUS "Curl found ${CURL_DIR}" )
    set(CURL_FOUND "Yes")
else(CURL_DIR)
    message(STATUS "Curl missing" )
endif()

#------FREETYPE-------------------------------------------------
FIND_PATH(FREETYPE_DIR Android.mk
    ${3RD_PARTY_PATH}/3rdparty/freetype NO_CMAKE_FIND_ROOT_PATH
)
set(FREETYPE_INCLUDE_DIRS ${FREETYPE_DIR}/include ${FREETYPE_DIR}/include/freetype/config)
if(FREETYPE_DIR)
    message(STATUS "FREETYPE found ${FREETYPE_DIR}" )
    set(FREETYPE_FOUND "Yes")
else(FREETYPE_DIR)
    message(STATUS "FREETYPE missing" )
endif()

#------GDAL----------------------------------------------------
FIND_PATH(GDAL_INCLUDE_DIR gdal.h
    ${3RD_PARTY_PATH}/3rdparty/gdal NO_CMAKE_FIND_ROOT_PATH
)
if(GDAL_INCLUDE_DIR)
    message(STATUS "GDAL found ${GDAL_INCLUDE_DIR}" )
    set(GDAL_FOUND "Yes")
else(GDAL_INCLUDE_DIR)
    message(STATUS "GDAL missing" )
endif()
ENDMACRO()
