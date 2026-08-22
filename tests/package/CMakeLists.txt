cmake_minimum_required(VERSION 3.16)
project(zget_package_consumer LANGUAGES C)

# The package-consumer project is intentionally standalone: it must exercise
# only the installed CMake package, not reach back into zget's source tree.
# CI passes the release version derived from the repository's single VERSION
# file so this test never grows a second manually maintained version literal.
if(NOT DEFINED ZGET_REQUIRED_VERSION OR
   NOT ZGET_REQUIRED_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR
        "ZGET_REQUIRED_VERSION must be provided as MAJOR.MINOR.PATCH")
endif()
find_package(Zget ${ZGET_REQUIRED_VERSION} REQUIRED)

# The installed export must carry the right usage requirements for both forms.
add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE Zget::libzget)
add_executable(consumer_static main.c)
target_link_libraries(consumer_static PRIVATE Zget::libzget_static)
