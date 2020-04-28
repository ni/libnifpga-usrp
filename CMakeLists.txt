cmake_minimum_required(VERSION 2.4.0)

project(nifpga)
enable_testing()

set(CMAKE_CXX_STANDARD 17)

include_directories(include)

add_library(nifpga SHARED
    src/Bitfile.cpp
    src/DeviceFile.cpp
    src/ErrnoMap.cpp
    src/Fifo.cpp
    src/FifoInfo.cpp
    src/NiFpga.cpp
    src/RegisterInfo.cpp
    src/ResourceInfo.cpp
    src/Session.cpp
    src/SysfsFile.cpp
    src/Type.cpp
)

set_target_properties(nifpga PROPERTIES CXX_VISIBILITY_PRESET hidden)

if(ENABLE_VALGRIND)
    target_compile_definitions(nifpga PRIVATE ENABLE_VALGRIND)
endif(ENABLE_VALGRIND)
target_link_options(nifpga PRIVATE "LINKER:-z,defs")

add_executable(lvbitx2dtso
    src/Bitfile.cpp
    src/dtgen.cpp
    src/FifoInfo.cpp
    src/lvbitx2dtso.cpp
    src/RegisterInfo.cpp
    src/ResourceInfo.cpp
    src/Type.cpp
    # only needed if this starts to output the.bit as well:
    # src / libb64 / cdecode.c
)

add_custom_command(
    OUTPUT libnifpga.so.symalias
    DEPENDS src/libnifpga.exports
    COMMAND sed -n "s/NiFpga_\\(\\w\\+\\)/-defsym=NiFpgaDll_\\1=&/p" ${CMAKE_SOURCE_DIR}/src/libnifpga.exports > libnifpga.so.symalias VERBATIM
    COMMENT "Generate compatibility aliases"
)
add_custom_target(nifpga_symalias DEPENDS libnifpga.so.symalias)

target_link_options(nifpga PRIVATE "LINKER:@libnifpga.so.symalias")
target_link_options(nifpga PRIVATE "LINKER:--version-script=libnifpga.so.map")

add_custom_command(
    OUTPUT libnifpga.so.map
    DEPENDS src/libnifpga.exports
    COMMAND echo "{" > libnifpga.so.map
	COMMAND echo "    global:" >> libnifpga.so.map
    COMMAND sed "/^NiFpga_/p; s/NiFpga_/NiFpgaDll_/" < ${CMAKE_SOURCE_DIR}/src/libnifpga.exports | sed "s/.*/        &;/" >> libnifpga.so.map VERBATIM
	COMMAND echo "    local:" >> libnifpga.so.map
	COMMAND echo "        *;" >> libnifpga.so.map
	COMMAND echo "};" >> libnifpga.so.map
    COMMENT "Generate version script"
)
add_custom_target(nifpga_map DEPENDS libnifpga.so.map)

add_dependencies(nifpga nifpga_map nifpga_symalias)

install(TARGETS nifpga DESTINATION lib)
install(TARGETS lvbitx2dtso DESTINATION bin)
file(GLOB HEADERS include/*.h)
install(FILES ${HEADERS} DESTINATION include)

add_executable(test_packedarray
    tests/test_PackedArray.cpp
)

add_test(NAME test_packedarray COMMAND test_packedarray)
