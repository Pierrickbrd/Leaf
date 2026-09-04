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
# machine with an odd locale builds the same bytes as this one. Every literal in this client
# is UTF-16 (`u"…"_s`), so a title in any script survives from the source to the screen, and
# no literal has to be judged ASCII by whoever writes it.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network Test)
# The session keyring — GNOME's or KDE's, and the code never learns which.
find_package(Qt6Keychain REQUIRED)

qt_standard_project_setup(REQUIRES 6.4)

qt_add_executable(leaf-desktop
    src/main.cpp
    src/Boot.h
    src/Boot.cpp
    src/Settings.h
    src/Settings.cpp
    src/Server.h
    src/Server.cpp
    src/Api.h
    src/Api.cpp
    src/Theme.h
    src/Theme.cpp
    src/Words.h
    src/Words.cpp
    src/Fonts.h
    src/Fonts.cpp
    src/Widths.h
    src/Widths.cpp
    src/Navigation.h
    src/Navigation.cpp
    src/Shelf.h
    src/Shelf.cpp
    src/Covers.h
    src/Covers.cpp
)

# Every QML file is addressed under the module by its path below `qml/`, so the tree on disk
# and the tree the engine sees are the same tree. Without the alias each file would answer at
# `qrc:/qt/qml/Leaf/qml/…`, carrying the folder it happens to live in — a detail of the
# repository leaking into every import.
set(LEAF_QML
    qml/Main.qml
    qml/FocusRing.qml
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

# The fonts live at the root of the repository, shared, and are not copied here: duplicating
# font binaries to respect a build boundary would be paying dearly for a principle.
set(LEAF_FONTS
    ${CMAKE_CURRENT_SOURCE_DIR}/../assets/fonts/BarlowCondensed-SemiBold.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/../assets/fonts/BarlowCondensed-Bold.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/../assets/fonts/Inter_18pt-Regular.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/../assets/fonts/Inter_18pt-Medium.ttf
)
qt_add_resources(leaf-desktop "leaf-fonts"
    PREFIX "/fonts"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../assets/fonts"
    FILES ${LEAF_FONTS})

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
    tests/talks_to_the_server.cpp tests/Pretend.h
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
    src/Api.h src/Words.h src/Words.cpp)
target_include_directories(writes-french PRIVATE src)
target_link_libraries(writes-french PRIVATE Qt6::Core Qt6::Qml Qt6::Test)
add_test(NAME writes-french COMMAND writes-french)

qt_add_executable(keeps-its-contrast tests/keeps_its_contrast.cpp
    src/Theme.h src/Theme.cpp src/Fonts.h src/Fonts.cpp)
target_include_directories(keeps-its-contrast PRIVATE src)
target_link_libraries(keeps-its-contrast PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Test)
add_test(NAME keeps-its-contrast COMMAND keeps-its-contrast)

qt_add_executable(embeds-its-fonts tests/embeds_its_fonts.cpp src/Fonts.h src/Fonts.cpp)
target_include_directories(embeds-its-fonts PRIVATE src)
target_link_libraries(embeds-its-fonts PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
qt_add_resources(embeds-its-fonts "test-fonts"
    PREFIX "/fonts"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../assets/fonts"
    FILES ${LEAF_FONTS})
add_test(NAME embeds-its-fonts COMMAND embeds-its-fonts)

qt_add_executable(knows-its-widths tests/knows_its_widths.cpp
    src/Widths.h src/Widths.cpp src/Words.h src/Words.cpp src/Api.h)
target_include_directories(knows-its-widths PRIVATE src)
target_link_libraries(knows-its-widths PRIVATE Qt6::Core Qt6::Qml Qt6::Test)
add_test(NAME knows-its-widths COMMAND knows-its-widths)

qt_add_executable(finds-its-way-back tests/finds_its_way_back.cpp
    src/Navigation.h src/Navigation.cpp src/Words.h src/Words.cpp src/Api.h)
target_include_directories(finds-its-way-back PRIVATE src)
target_link_libraries(finds-its-way-back PRIVATE Qt6::Core Qt6::Qml Qt6::Test)
add_test(NAME finds-its-way-back COMMAND finds-its-way-back)

# The only test that loads real QML through a real engine. The eight tests on Navigation and
# the six on Widths all reach the C++ directly; `opens.sh` proves a window comes up at all —
# but nothing between them proved that `import Leaf` in a `.qml` file actually resolves to
# these same objects. This target runs `Boot::run` exactly as `main` does, headless, and reads
# the singletons back afterwards to prove the seam holds both ways: the window picked up
# `Widths.window` from its own width, and `Theme` and `Navigation` answer as the C++ says they
# should.
qt_add_executable(crosses-the-seam
    tests/crosses_the_seam.cpp
    src/Boot.h src/Boot.cpp
    src/Covers.h src/Covers.cpp
    src/Settings.h src/Settings.cpp
    src/Server.h src/Server.cpp
    src/Fonts.h src/Fonts.cpp
    src/Theme.h src/Theme.cpp
    src/Words.h src/Words.cpp
    src/Widths.h src/Widths.cpp
    src/Navigation.h src/Navigation.cpp
    src/Api.h
)
target_include_directories(crosses-the-seam PRIVATE src)
qt_add_qml_module(crosses-the-seam
    URI Leaf
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    # Its own directory: the default is named from the URI alone, and `leaf-desktop` already
    # claims "Leaf" there. Same URI, same QML — this is the second build of one module, not a
    # second module, so only the output path needs to differ.
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/crosses-the-seam-qml/Leaf"
    QML_FILES ${LEAF_QML}
)
qt_add_resources(crosses-the-seam "leaf-fonts-for-the-seam-test"
    PREFIX "/fonts"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../assets/fonts"
    FILES ${LEAF_FONTS})
target_link_libraries(crosses-the-seam PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Network Qt6::Qml Qt6::Test
    ${QTKEYCHAIN_LIBRARIES})
add_test(NAME crosses-the-seam COMMAND crosses-the-seam)

# The shelf's model, against the same forty-line server. It holds rows, pages and says why it
# is empty — none of which needs a window, so none of it waits on the grid being written.
qt_add_executable(holds-a-shelf
    tests/holds_a_shelf.cpp tests/Pretend.h
    src/Settings.h src/Settings.cpp src/Server.h src/Server.cpp
    src/Api.h src/Api.cpp src/Words.h src/Words.cpp
    src/Navigation.h src/Navigation.cpp src/Widths.h src/Widths.cpp
    src/Shelf.h src/Shelf.cpp)
target_include_directories(holds-a-shelf PRIVATE src)
target_link_libraries(holds-a-shelf PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network Qt6::Test ${QTKEYCHAIN_LIBRARIES})
add_test(NAME holds-a-shelf COMMAND holds-a-shelf)

# The key on the covers QML fetches for itself. Its live half needs two servers at once — the
# one configured, and another that must never be told the key.
qt_add_executable(puts-the-key-where-it-belongs
    tests/puts_the_key_where_it_belongs.cpp tests/Pretend.h
    src/Settings.h src/Settings.cpp src/Server.h src/Server.cpp
    src/Covers.h src/Covers.cpp)
target_include_directories(puts-the-key-where-it-belongs PRIVATE src)
target_link_libraries(puts-the-key-where-it-belongs PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network Qt6::Test ${QTKEYCHAIN_LIBRARIES})
add_test(NAME puts-the-key-where-it-belongs COMMAND puts-the-key-where-it-belongs)

qt_add_executable(against-the-real-server EXCLUDE_FROM_ALL
    tests/against_real.cpp
    src/Settings.h src/Settings.cpp src/Server.h src/Server.cpp)
target_include_directories(against-the-real-server PRIVATE src)
target_link_libraries(against-the-real-server PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network Qt6::Test ${QTKEYCHAIN_LIBRARIES})

# The two Python guards. They run here as well as in continuous integration, because a check
# that only ever runs on a server is a check nobody notices going quiet.
add_test(NAME knows-the-contract
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/client_knows_the_contract.py)

add_test(NAME bytes-stay-utf8
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/bytes_stay_utf8.py)

set_tests_properties(keeps-the-key talks-to-the-server keeps-its-contrast embeds-its-fonts
    crosses-the-seam holds-a-shelf puts-the-key-where-it-belongs
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

install(TARGETS leaf-desktop
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
