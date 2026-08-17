cmake_minimum_required(VERSION 3.16)
project(zget_package_consumer LANGUAGES C)
find_package(Zget 0.1 REQUIRED)
# The installed export must carry the right usage requirements for both forms.
add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE Zget::libzget)
add_executable(consumer_static main.c)
target_link_libraries(consumer_static PRIVATE Zget::libzget_static)
