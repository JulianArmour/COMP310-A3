cmake_minimum_required(VERSION 3.10)
project(simple_file_system C)

set(CMAKE_C_STANDARD 99)

#find_package(pkgConfig REQUIRED)
#pkg_check_modules(FUSE REQUIRED fuse)

add_library(Disk disk_emu.c)
add_library(SFS sfs_api.c)

add_executable(sfs_test sfs_test.c)
add_executable(sfs_test2 sfs_test2.c)

target_link_libraries(sfs_test DISK SFS)
target_link_libraries(sfs_test2 DISK SFS)

#target_link_libraries(sfs_test ${FUSE_LIBRARIES})
#target_include_directories(sfs_test PUBLIC ${FUSE_INCLUDE_DIR})