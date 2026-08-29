# The Ubuntu client. Qt 6 with QML, and the logic behind it in C++.
#
# Why the two rather than QML alone: everything that has to be right lives in C++ — talking
# to the server, holding the model, deciding what a screen means — and QML describes the
# screen. The boundary is a handful of QObject types exposed to the engine, so the parts
# that need testing can be tested without a window.
cmake_minimum_required(VERSION 3.21)

project(leaf-desktop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Warnings are errors, as they are on the server. This block has one chance to establish
# that, and it is now — a codebase that has learnt to ignore its warnings never unlearns it.
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# The sources are UTF-8, and so is what they compile to — pinned rather than inherited, so a
# machine with an odd locale builds the same bytes as this one. Ascii.h holds the other half
# of this: what UTF-8 must never be read back as.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network Test)
# The session keyring — GNOME's or KDE's, and the code never learns which.
find_package(Qt6Keychain REQUIRED)

qt_standard_project_setup(REQUIRES 6.4)

qt_add_executable(leaf-desktop
    src/main.cpp
    src/Settings.h
    src/Settings.cpp
    src/Server.h
    src/Server.cpp
    src/Api.h
    src/Api.cpp
    src/Words.h
    src/Words.cpp
)

# Every QML file is addressed under the module by its path below `qml/`, so the tree on disk
# and the tree the engine sees are the same tree. Without the alias each file would answer at
# `qrc:/qt/qml/Leaf/qml/…`, carrying the folder it happens to live in — a detail of the
# repository leaking into every import.
set(LEAF_QML
    qml/Main.qml
)
foreach(qml_file IN LISTS LEAF_QML)
    string(REGEX REPLACE "^qml/" "" alias "${qml_file}")
    set_source_files_properties(${qml_file} PROPERTIES QT_RESOURCE_ALIAS "${alias}")
endforeach()

qt_add_qml_module(leaf-desktop
    URI Leaf
    VERSION 1.0
    # Spelled out, because the default moved: Qt 6.4 puts a module at `:/Leaf`, 6.5 and after
    # at `:/qt/qml/Leaf`. Fixing it here means the path in main.cpp is right on both, and is
    # the one `loadFromModule` resolves to on the versions that have it.
    RESOURCE_PREFIX "/qt/qml"
    QML_FILES ${LEAF_QML}
)

# The QML type registration Qt generates includes the headers by their bare name.
target_include_directories(leaf-desktop PRIVATE src)

target_link_libraries(leaf-desktop PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    ${QTKEYCHAIN_LIBRARIES}
)

enable_testing()

qt_add_executable(keeps-the-key tests/keeps_the_key.cpp src/Settings.h src/Settings.cpp)
target_include_directories(keeps-the-key PRIVATE src)
target_link_libraries(keeps-the-key PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Test ${QTKEYCHAIN_LIBRARIES})
# Offscreen, so it runs the same on a machine with no display and in a container.
add_test(NAME keeps-the-key COMMAND keeps-the-key)

qt_add_executable(talks-to-the-server
    tests/talks_to_the_server.cpp
    src/Settings.h src/Settings.cpp src/Server.h src/Server.cpp)
target_include_directories(talks-to-the-server PRIVATE src)
target_link_libraries(talks-to-the-server PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network Qt6::Test ${QTKEYCHAIN_LIBRARIES})
add_test(NAME talks-to-the-server COMMAND talks-to-the-server)

# Nothing here needs a window, an event loop or a keyring — it is a parser and a pile of
# JSON — so it runs applessly and stays the fastest test in the set.
qt_add_executable(reads-the-contract tests/reads_the_contract.cpp src/Api.h src/Api.cpp)
target_include_directories(reads-the-contract PRIVATE src)
target_link_libraries(reads-the-contract PRIVATE Qt6::Core Qt6::Test)
add_test(NAME reads-the-contract COMMAND reads-the-contract)

qt_add_executable(writes-french tests/writes_french.cpp
    src/Api.h src/Api.cpp src/Words.h src/Words.cpp)
target_include_directories(writes-french PRIVATE src)
target_link_libraries(writes-french PRIVATE Qt6::Core Qt6::Test)
add_test(NAME writes-french COMMAND writes-french)

# Built only by the test that insists it fails. Plain add_executable: there is no QObject
# here, and no reason to run moc over a file whose purpose is to be rejected.
add_executable(mangles-utf8 EXCLUDE_FROM_ALL tests/mangles_utf8.cpp)
target_include_directories(mangles-utf8 PRIVATE src)
target_link_libraries(mangles-utf8 PRIVATE Qt6::Core)
# The spellings no compiler can be made to refuse: QLatin1String("é") written out in full,
# and toLatin1(), which turns what it has no room for into a '?'.
# Run here and not only in CI. It was CI-only, so when the Latin-1 work renamed the literal
# suffix this stopped matching anything and nobody found out until it was asked by hand.
add_test(NAME knows-the-contract
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/client_knows_the_contract.py)

add_test(NAME bytes-stay-utf8
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/bytes_stay_utf8.py)

add_executable(reaches-for-l1 EXCLUDE_FROM_ALL tests/reaches_for_l1.cpp)
target_include_directories(reaches-for-l1 PRIVATE src)
target_link_libraries(reaches-for-l1 PRIVATE Qt6::Core)

add_test(NAME refuses-latin1
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/refuses_latin1.sh ${CMAKE_BINARY_DIR})

qt_add_executable(against-the-real-server EXCLUDE_FROM_ALL
    tests/against_real.cpp
    src/Settings.h src/Settings.cpp src/Server.h src/Server.cpp)
target_include_directories(against-the-real-server PRIVATE src)
target_link_libraries(against-the-real-server PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network Qt6::Test ${QTKEYCHAIN_LIBRARIES})

set_tests_properties(keeps-the-key talks-to-the-server
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

install(TARGETS leaf-desktop
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
