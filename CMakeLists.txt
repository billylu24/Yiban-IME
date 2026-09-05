cmake_minimum_required(VERSION 3.20)

project(bilingual-ime VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)

option(BILINGUAL_BUILD_ADDON "Build the Fcitx5 sentence translation addon" ON)
if(BILINGUAL_BUILD_ADDON)
    find_package(nlohmann_json 3.11 REQUIRED)
    find_package(Fcitx5Core 5.1.22 REQUIRED)
    find_package(Fcitx5Config 5.1.22 REQUIRED)
else()
    find_package(nlohmann_json 3.11 QUIET)
endif()

set(CEDICT_ARCHIVE "${CMAKE_CURRENT_SOURCE_DIR}/data/dictionary/cedict_ts.u8.gz")
set(BILINGUAL_DICTIONARY "${CMAKE_CURRENT_BINARY_DIR}/dictionary/base.tsv")
add_custom_command(
    OUTPUT "${BILINGUAL_DICTIONARY}"
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/convert_cedict.py"
            "${CEDICT_ARCHIVE}" "${BILINGUAL_DICTIONARY}"
    DEPENDS "${CEDICT_ARCHIVE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/convert_cedict.py"
    COMMENT "Generating compact bilingual candidate dictionary"
    VERBATIM
)
add_custom_target(bilingual_dictionary_data ALL
    DEPENDS "${BILINGUAL_DICTIONARY}"
)

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

add_library(bilingual_sentence_state STATIC
    src/context/sentence_state.cpp
)
target_include_directories(bilingual_sentence_state PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
)
target_compile_features(bilingual_sentence_state PUBLIC cxx_std_20)
set_target_properties(bilingual_sentence_state PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

if(nlohmann_json_FOUND)
    add_library(bilingual_translation STATIC
        src/translation/protocol.cpp
        src/translation/translation_client.cpp
    )
    target_include_directories(bilingual_translation PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
    )
    target_compile_features(bilingual_translation PUBLIC cxx_std_20)
    target_link_libraries(bilingual_translation PUBLIC
        nlohmann_json::nlohmann_json
        Threads::Threads
    )
    set_target_properties(bilingual_translation PROPERTIES
        CXX_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON
    )

    if(BILINGUAL_BUILD_ADDON)
        add_fcitx5_addon(bilingualcontext
            src/addon/bilingual_context.cpp
        )
        target_include_directories(bilingualcontext PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
        )
        target_compile_features(bilingualcontext PRIVATE cxx_std_20)
        target_link_libraries(bilingualcontext PRIVATE
            Fcitx5::Core
            Fcitx5::Config
            bilingual_sentence_state
            bilingual_translation
        )
        install(TARGETS bilingualcontext
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5"
        )
        configure_file(data/bilingualcontext.conf.in bilingualcontext.conf @ONLY)
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/bilingualcontext.conf"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon"
        )
    endif()
endif()

option(YIBAN_BRAND_RIME "Display the Rime input method as Yiban" ON)
if(YIBAN_BRAND_RIME)
    install(FILES data/yiban-inputmethod.conf
        DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod" RENAME rime.conf)
    install(DIRECTORY data/icons/hicolor
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons")
endif()

# Standalone service payload: no checkout, Conda, or bundled Fcitx is required.
configure_file(scripts/yiban-translator.in yiban-translator @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/yiban-translator"
    DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES translator/daemon.py
    DESTINATION "${CMAKE_INSTALL_DATADIR}/yiban-ime/translator")
install(FILES config/bilingualcontext.conf
    DESTINATION "${CMAKE_INSTALL_DATADIR}/yiban-ime/config")
install(PROGRAMS scripts/install-translator-service.sh
    DESTINATION "${CMAKE_INSTALL_DATADIR}/yiban-ime/scripts")

install(TARGETS bilingual_dictionary
    EXPORT BilingualImeDictionaryTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(FILES src/dictionary/bilingual_dictionary.h
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/bilingual-ime/dictionary"
)
install(FILES "${BILINGUAL_DICTIONARY}"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/bilingual-ime/dictionary"
)
install(FILES data/dictionary/CC-CEDICT-LICENSE.txt
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
    add_dependencies(dictionary_test bilingual_dictionary_data)
    add_test(NAME dictionary_test
        COMMAND dictionary_test "${BILINGUAL_DICTIONARY}"
    )

    add_executable(sentence_state_test tests/sentence_state_test.cpp)
    target_link_libraries(sentence_state_test PRIVATE bilingual_sentence_state)
    add_test(NAME sentence_state_test COMMAND sentence_state_test)

    add_executable(cursor_sentence_test tests/cursor_sentence_test.cpp)
    target_link_libraries(cursor_sentence_test PRIVATE bilingual_sentence_state)
    add_test(NAME cursor_sentence_test COMMAND cursor_sentence_test)

    if(nlohmann_json_FOUND)
        add_executable(protocol_test tests/protocol_test.cpp)
        target_link_libraries(protocol_test PRIVATE bilingual_translation)
        add_test(NAME protocol_test COMMAND protocol_test)
    endif()

    add_test(NAME config_test
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/config_test.py"
    )

    add_test(NAME daemon_test
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/daemon_test.py"
    )
endif()

option(BILINGUAL_BUILD_BENCHMARKS "Build microbenchmarks" OFF)
if(BILINGUAL_BUILD_BENCHMARKS)
    add_executable(dictionary_benchmark benchmarks/dictionary_benchmark.cpp)
    target_link_libraries(dictionary_benchmark PRIVATE bilingual_dictionary)
endif()

message(STATUS "Yiban IME: sentence addon=${BILINGUAL_BUILD_ADDON}; prefix=${CMAKE_INSTALL_PREFIX}; libdir=${CMAKE_INSTALL_LIBDIR}")
