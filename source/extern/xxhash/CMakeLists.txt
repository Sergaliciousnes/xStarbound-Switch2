add_library(xxhash STATIC
        xxh3.h
        xxh_x86dispatch.h
        xxhash.c
        xxhash.h
)

if(STAR_ARCHITECTURE MATCHES "^(i386|x86_64)$")
    target_sources(xxhash PRIVATE xxh_x86dispatch.c)
endif()

target_include_directories(xxhash
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
