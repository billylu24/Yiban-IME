cmake_minimum_required(VERSION 3.20)

project(bilingual-ime VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

add_library(bilingual_dictionary STATIC
    src/dictionary/bilingual_dictionary.cpp
)
target_include_directories(bilingual_dictionary PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/bilingual-ime>"
)
target_compile_features(bilingual_dictionary PUBLIC cxx_std_20)
set_target_properties(bilingual_dictionary PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME Dictionary
    POSITION_INDEPENDENT_CODE ON
)
add_library(BilingualIme::Dictionary ALIAS bilingual_dictionary)

install(TARGETS bilingual_dictionary
    EXPORT BilingualImeDictionaryTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(FILES src/dictionary/bilingual_dictionary.h
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/bilingual-ime/dictionary"
)
install(FILES data/dictionary/base.tsv
    DESTINATION "${CMAKE_INSTALL_DATADIR}/bilingual-ime/dictionary"
)
install(EXPORT BilingualImeDictionaryTargets
    FILE BilingualImeDictionaryTargets.cmake
    NAMESPACE BilingualIme::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/BilingualImeDictionary"
)
configure_package_config_file(
    cmake/BilingualImeDictionaryConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/BilingualImeDictionaryConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/BilingualImeDictionary"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/BilingualImeDictionaryConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/BilingualImeDictionaryConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/BilingualImeDictionaryConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/BilingualImeDictionary"
)

if(BUILD_TESTING)
    add_executable(dictionary_test tests/dictionary_test.cpp)
    target_link_libraries(dictionary_test PRIVATE bilingual_dictionary)
    add_test(NAME dictionary_test COMMAND dictionary_test)
endif()

option(BILINGUAL_BUILD_BENCHMARKS "Build microbenchmarks" OFF)
if(BILINGUAL_BUILD_BENCHMARKS)
    add_executable(dictionary_benchmark benchmarks/dictionary_benchmark.cpp)
    target_link_libraries(dictionary_benchmark PRIVATE bilingual_dictionary)
endif()
