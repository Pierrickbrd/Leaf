# The Ubuntu client. Qt 6 with QML, and the logic behind it in C++.
#
# Why the two rather than QML alone: everything that has to be right lives in C++ — talking
# to the server, holding the model, deciding what a screen means — and QML describes the
# screen. The boundary is a handful of QObject types exposed to the engine, so the parts
# that need testing can be tested without a window.
cmake_minimum_required(VERSION 3.21)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" LEAF_VERSION LIMIT_COUNT 1)
project(leaf-desktop VERSION ${LEAF_VERSION} LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)
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

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network Concurrent Test)
# The session keyring — GNOME's or KDE's, and the code never learns which.
find_package(Qt6Keychain REQUIRED)
# The archive reader unpacks one small member of a CBZ and never a page — see Cbz.h.
find_package(ZLIB REQUIRED)

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
    src/Rearrange.cpp
    src/Preferences.h
    src/Preferences.cpp
    src/Health.h
    src/Health.cpp
    src/Scan.h
    src/Scan.cpp
    src/Shelf.cpp
    src/Cbz.h
    src/Cbz.cpp
    src/Manifest.h
    src/Manifest.cpp
    src/Card.h
    src/Card.cpp
    src/Carrier.h
    src/Carrier.cpp
    src/Imports.h
    src/Imports.cpp
    src/ImportCaptions.h
    src/ImportCaptions.cpp
    src/CardCaptions.h
    src/CardCaptions.cpp
    src/Search.h
    src/Search.cpp
    src/Captions.h
    src/Captions.cpp
    src/Filters.h
    src/Filters.cpp
    src/Resume.h
    src/Resume.cpp
    src/Series.h
    src/Series.cpp
    src/Entries.h
    src/Entries.cpp
    src/SeriesCaptions.h
    src/SeriesCaptions.cpp
    src/Elsewhere.h
    src/Elsewhere.cpp
    src/Erasure.h
    src/Erasure.cpp
    src/Commands.h
    src/Commands.cpp
    src/Covers.h
    src/Covers.cpp
)

# Every QML file is addressed under the module by its path below `qml/`, so the tree on disk
# and the tree the engine sees are the same tree. Without the alias each file would answer at
# `qrc:/qt/qml/Leaf/qml/…`, carrying the folder it happens to live in — a detail of the
# repository leaking into every import.
set(LEAF_QML
    qml/Main.qml
    qml/NavigationCursor.qml
    qml/CardAction.qml
    qml/FocusRing.qml
    qml/ScrollVeil.qml
    qml/ActionButton.qml
    qml/BarButton.qml
    qml/LeafToolTip.qml
    qml/LeafMenu.qml
    qml/LeafMenuItem.qml
    qml/AppBar.qml
    qml/FilterChip.qml
    qml/FilterPills.qml
    qml/LeafCheck.qml
    qml/LevelMark.qml
    qml/ImportDialog.qml
    qml/ImportNode.qml
    qml/ImportRow.qml
    qml/SeriesTile.qml
    qml/ShelfSkeleton.qml
    qml/SettingsView.qml
    qml/SettingsTabs.qml
    qml/SettingsCard.qml
    qml/SettingsChoice.qml
    qml/SettingsStatus.qml
    qml/SettingsLine.qml
    qml/SettingsNote.qml
    qml/FilterPanel.qml
    qml/FilterAxis.qml
    qml/FilterValueRow.qml
    qml/LeafTextAction.qml
    qml/LeafField.qml
    qml/LeafSearchLine.qml
    qml/SearchSeriesGrid.qml
    qml/FileResultRow.qml
    qml/FileResultsView.qml
    qml/SearchLink.qml
    qml/SearchScopeTabs.qml
    qml/SearchWorkspace.qml
    qml/CoverShadow.qml
    qml/ResumeBand.qml
    qml/ShelfView.qml
    qml/SeriesView.qml
    qml/SeriesHeader.qml
    qml/SeriesTabs.qml
    qml/SeriesDescription.qml
    qml/SeriesSkeleton.qml
    qml/FactColumn.qml
    qml/VolumeRow.qml
    qml/ArcRow.qml
    qml/ChapterRange.qml
    qml/VolumeState.qml
    qml/ElsewhereTab.qml
    qml/ElsewhereBlock.qml
    qml/BlockHeading.qml
    qml/TileShelf.qml
    qml/MenuAction.qml
    qml/CommandMenu.qml
    qml/EraseDialog.qml
    qml/VolumeTile.qml
)
foreach(qml_file IN LISTS LEAF_QML)
    string(REGEX REPLACE "^qml/" "" alias "${qml_file}")
    set_source_files_properties(${qml_file} PROPERTIES QT_RESOURCE_ALIAS "${alias}")
endforeach()

set(LEAF_IMAGES
    qml/assets/cover-shadow-light.png
    qml/assets/cover-shadow-dark.png
    qml/assets/icons/arrow_back.svg
    qml/assets/icons/book.svg
    qml/assets/icons/book_2.svg
    qml/assets/icons/bookmark.svg
    qml/assets/icons/check.svg
    qml/assets/icons/close.svg
    qml/assets/icons/cloud_done.svg
    qml/assets/icons/cloud_off.svg
    qml/assets/icons/collections_bookmark.svg
    qml/assets/icons/contrast.svg
    qml/assets/icons/dark_mode.svg
    qml/assets/icons/delete.svg
    qml/assets/icons/done_all.svg
    qml/assets/icons/download.svg
    qml/assets/icons/library_add.svg
    qml/assets/icons/light_mode.svg
    qml/assets/icons/more_vert.svg
    qml/assets/icons/public.svg
    qml/assets/icons/refresh.svg
    qml/assets/icons/remove_done.svg
    qml/assets/icons/search.svg
    qml/assets/icons/settings.svg
    qml/assets/icons/sort.svg
    qml/assets/icons/tune.svg
    qml/assets/icons/upload.svg
)
foreach(image_file IN LISTS LEAF_IMAGES)
    string(REGEX REPLACE "^qml/" "" alias "${image_file}")
    set_source_files_properties(${image_file} PROPERTIES QT_RESOURCE_ALIAS "${alias}")
endforeach()

set(LEAF_LOGOS
    ${CMAKE_CURRENT_SOURCE_DIR}/../assets/logos/leaf-light.svg
    ${CMAKE_CURRENT_SOURCE_DIR}/../assets/logos/leaf-dark.svg
    ${CMAKE_CURRENT_SOURCE_DIR}/../assets/logos/leaf-mark-light.svg
    ${CMAKE_CURRENT_SOURCE_DIR}/../assets/logos/leaf-mark-dark.svg
)
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/../assets/logos/leaf-light.svg
    PROPERTIES QT_RESOURCE_ALIAS "assets/logos/leaf-light.svg")
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/../assets/logos/leaf-dark.svg
    PROPERTIES QT_RESOURCE_ALIAS "assets/logos/leaf-dark.svg")
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/../assets/logos/leaf-mark-light.svg
    PROPERTIES QT_RESOURCE_ALIAS "assets/logos/leaf-mark-light.svg")
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/../assets/logos/leaf-mark-dark.svg
    PROPERTIES QT_RESOURCE_ALIAS "assets/logos/leaf-mark-dark.svg")

qt_add_qml_module(leaf-desktop
    URI Leaf
    VERSION 1.0
    DEPENDENCIES QtQuick
    # Spelled out, because the default moved: Qt 6.4 puts a module at `:/Leaf`, 6.5 and after
    # at `:/qt/qml/Leaf`. Fixing it here means the path in main.cpp is right on both, and is
    # the one `loadFromModule` resolves to on the versions that have it.
    RESOURCE_PREFIX "/qt/qml"
    QML_FILES ${LEAF_QML}
    RESOURCES ${LEAF_IMAGES} ${LEAF_LOGOS}
)

# The QML type registration Qt generates includes the headers by their bare name.
target_include_directories(leaf-desktop PRIVATE src)
target_compile_definitions(leaf-desktop PRIVATE LEAF_VERSION="${PROJECT_VERSION}")

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
    Qt6::Concurrent
    ZLIB::ZLIB
    ${QTKEYCHAIN_LIBRARIES}
)

enable_testing()

add_test(NAME reports-version COMMAND leaf-desktop --version)
add_test(NAME checks-startup COMMAND leaf-desktop --check-startup)
set_tests_properties(reports-version checks-startup
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(keeps-the-key tests/keeps_the_key.cpp src/Settings.h src/Settings.cpp src/Words.h src/Words.cpp)
target_include_directories(keeps-the-key PRIVATE src)
target_link_libraries(keeps-the-key PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Test ${QTKEYCHAIN_LIBRARIES})
# Offscreen, so it runs the same on a machine with no display and in a container.
add_test(NAME keeps-the-key COMMAND keeps-the-key)

qt_add_executable(talks-to-the-server
    tests/talks_to_the_server.cpp tests/Pretend.h
    src/Settings.h src/Settings.cpp src/Words.h src/Words.cpp src/Server.h src/Server.cpp)
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

# `Cbz` and zlib for a test that reads no archive: `Manifest`'s walk opens one per file to
# read the title it declares, which the tree shows beside the file name.
qt_add_executable(writes-french tests/writes_french.cpp
    src/Api.h src/Words.h src/Words.cpp src/Manifest.h src/Manifest.cpp
    src/Cbz.h src/Cbz.cpp)
target_include_directories(writes-french PRIVATE src)
target_link_libraries(writes-french PRIVATE Qt6::Core Qt6::Qml Qt6::Test ZLIB::ZLIB)
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

# The only test that loads real QML through a real engine. The tests on Navigation and Widths
# all reach the C++ directly; `opens.sh` proves a window comes up at all —
# but nothing between them proved that `import Leaf` in a `.qml` file actually resolves to
# these same objects. This target runs `Boot::run` exactly as `main` does, headless, and reads
# the singletons back afterwards to prove the seam holds both ways: the window picked up
# `Widths.window` from its own width, and `Theme` and `Navigation` answer as the C++ says they
# should.
qt_add_executable(crosses-the-seam
    tests/crosses_the_seam.cpp tests/Pretend.h
    src/Boot.h src/Boot.cpp
    src/Covers.h src/Covers.cpp
    src/Card.h src/Card.cpp src/Carrier.h src/Carrier.cpp
    src/Imports.h src/Imports.cpp src/ImportCaptions.h src/ImportCaptions.cpp
    src/CardCaptions.h src/CardCaptions.cpp
    src/Cbz.h src/Cbz.cpp src/Manifest.h src/Manifest.cpp
    src/Settings.h src/Settings.cpp
    src/Server.h src/Server.cpp
    src/Fonts.h src/Fonts.cpp
    src/Theme.h src/Theme.cpp
    src/Words.h src/Words.cpp
    src/Widths.h src/Widths.cpp
    src/Navigation.h src/Navigation.cpp
    src/Api.h src/Api.cpp
    src/Rearrange.h src/Rearrange.cpp
    src/Shelf.h src/Shelf.cpp
    src/Search.h src/Search.cpp
    src/Captions.h src/Captions.cpp
    src/Filters.h src/Filters.cpp
    src/Health.h src/Health.cpp
    src/Scan.h src/Scan.cpp
    src/Preferences.h src/Preferences.cpp
    src/Resume.h src/Resume.cpp
    src/Series.h src/Series.cpp
    src/Entries.h src/Entries.cpp
    src/SeriesCaptions.h src/SeriesCaptions.cpp
    src/Elsewhere.h src/Elsewhere.cpp
    src/Erasure.h src/Erasure.cpp
    src/Commands.h src/Commands.cpp
)
target_include_directories(crosses-the-seam PRIVATE src)
qt_add_qml_module(crosses-the-seam
    URI Leaf
    VERSION 1.0
    DEPENDENCIES QtQuick
    RESOURCE_PREFIX "/qt/qml"
    # Its own directory: the default is named from the URI alone, and `leaf-desktop` already
    # claims "Leaf" there. Same URI, same QML — this is the second build of one module, not a
    # second module, so only the output path needs to differ.
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/crosses-the-seam-qml/Leaf"
    QML_FILES ${LEAF_QML}
    RESOURCES ${LEAF_IMAGES} ${LEAF_LOGOS}
)
qt_add_resources(crosses-the-seam "leaf-fonts-for-the-seam-test"
    PREFIX "/fonts"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../assets/fonts"
    FILES ${LEAF_FONTS})
target_link_libraries(crosses-the-seam PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Network Qt6::Qml Qt6::Concurrent Qt6::Test
    ZLIB::ZLIB ${QTKEYCHAIN_LIBRARIES})
add_test(NAME crosses-the-seam COMMAND crosses-the-seam)

# The shelf's model, against the same forty-line server. It holds rows, pages and says why it
# is empty — none of which needs a window, so none of it waits on the grid being written.
qt_add_executable(holds-a-shelf
    tests/holds_a_shelf.cpp tests/Pretend.h
    src/Settings.h src/Settings.cpp src/Server.h src/Server.cpp
    src/Api.h src/Api.cpp src/Words.h src/Words.cpp
    src/Navigation.h src/Navigation.cpp src/Widths.h src/Widths.cpp
    src/Rearrange.h src/Rearrange.cpp src/Shelf.h src/Shelf.cpp)
target_include_directories(holds-a-shelf PRIVATE src)
target_link_libraries(holds-a-shelf PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network Qt6::Test ${QTKEYCHAIN_LIBRARIES})
add_test(NAME holds-a-shelf COMMAND holds-a-shelf)

# The file rows returned beside the series grid. Like the shelf, this is HTTP state and words,
# not pixels, so every branch is exercised without constructing a window.
qt_add_executable(holds-the-imports
    tests/holds_the_imports.cpp tests/Pretend.h
    src/Card.h src/Card.cpp src/Carrier.h src/Carrier.cpp
    src/Imports.h src/Imports.cpp src/Cbz.h src/Cbz.cpp src/Manifest.h src/Manifest.cpp
    src/Api.h src/Api.cpp src/Words.h src/Words.cpp
    src/Navigation.h src/Navigation.cpp src/Widths.h src/Widths.cpp
    src/Server.h src/Server.cpp src/Settings.h src/Settings.cpp)
target_include_directories(holds-the-imports PRIVATE src)
target_link_libraries(holds-the-imports PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network Qt6::Concurrent Qt6::Test ZLIB::ZLIB ${QTKEYCHAIN_LIBRARIES})
add_test(NAME holds-the-imports COMMAND holds-the-imports)

# `Words.cpp` and `Qt6::Qml` for a test that draws nothing: the sentences these two
# modules show a reader live in `Words` like every other, and `Words.h` reaches
# `Navigation.h`, which is a QML element. The alternative was leaving nine French sentences
# where `tools/words_stay_french.py` cannot see them — one of them had carried an ordinary
# space before its « ? » since it was written.
qt_add_executable(describes-a-folder
    tests/describes_a_folder.cpp
    src/Manifest.h src/Manifest.cpp
    src/Cbz.h src/Cbz.cpp
    src/Words.h src/Words.cpp)
target_include_directories(describes-a-folder PRIVATE src)
target_link_libraries(describes-a-folder PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Test ZLIB::ZLIB)
add_test(NAME describes-a-folder COMMAND describes-a-folder)

qt_add_executable(opens-an-archive
    tests/opens_an_archive.cpp
    src/Cbz.h src/Cbz.cpp
    src/Words.h src/Words.cpp)
target_include_directories(opens-an-archive PRIVATE src)
target_link_libraries(opens-an-archive PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Test ZLIB::ZLIB)
add_test(NAME opens-an-archive COMMAND opens-an-archive)

qt_add_executable(keeps-a-preference
    tests/keeps_a_preference.cpp
    src/Preferences.h src/Preferences.cpp src/Words.h src/Words.cpp
    src/Api.h src/Api.cpp src/Navigation.h src/Navigation.cpp
    src/Widths.h src/Widths.cpp)
target_include_directories(keeps-a-preference PRIVATE src)
target_link_libraries(keeps-a-preference PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Test)
add_test(NAME keeps-a-preference COMMAND keeps-a-preference)

qt_add_executable(holds-the-search
    tests/holds_the_search.cpp tests/Pretend.h
    src/Settings.h src/Settings.cpp src/Server.h src/Server.cpp
    src/Api.h src/Api.cpp src/Words.h src/Words.cpp
    src/Navigation.h src/Navigation.cpp src/Widths.h src/Widths.cpp
    src/Rearrange.h src/Rearrange.cpp
    src/Shelf.h src/Shelf.cpp src/Search.h src/Search.cpp
    src/Captions.h src/Captions.cpp)
target_include_directories(holds-the-search PRIVATE src)
target_link_libraries(holds-the-search PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network Qt6::Test ${QTKEYCHAIN_LIBRARIES})
add_test(NAME holds-the-search COMMAND holds-the-search)

# The first card from /next. It owns state and wording, so it stays headless like the shelf.
qt_add_executable(holds-the-resume
    tests/holds_the_resume.cpp tests/Pretend.h
    src/Settings.h src/Settings.cpp src/Server.h src/Server.cpp
    src/Api.h src/Api.cpp src/Words.h src/Words.cpp
    src/Navigation.h src/Navigation.cpp src/Widths.h src/Widths.cpp
    src/Resume.h src/Resume.cpp)
target_include_directories(holds-the-resume PRIVATE src)
target_link_libraries(holds-the-resume PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network Qt6::Test ${QTKEYCHAIN_LIBRARIES})
add_test(NAME holds-the-resume COMMAND holds-the-resume)

# One series, as its own page draws it. Apart from the shelf's test because it answers a
# different question: not « what does a grid of many look like » but « what does a page of
# one say », which is where every French string of that screen is settled.
qt_add_executable(holds-the-series
    tests/holds_the_series.cpp tests/Pretend.h
    src/Settings.h src/Settings.cpp src/Server.h src/Server.cpp
    src/Api.h src/Api.cpp src/Words.h src/Words.cpp
    src/Navigation.h src/Navigation.cpp src/Widths.h src/Widths.cpp
    src/Series.h src/Series.cpp)
target_include_directories(holds-the-series PRIVATE src)
target_link_libraries(holds-the-series PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network Qt6::Test ${QTKEYCHAIN_LIBRARIES})
add_test(NAME holds-the-series COMMAND holds-the-series)

# The volumes tab: two answers married into one list, and the gaps that belong to neither.
qt_add_executable(holds-the-volumes
    tests/holds_the_volumes.cpp tests/Pretend.h
    src/Settings.h src/Settings.cpp src/Server.h src/Server.cpp
    src/Api.h src/Api.cpp src/Words.h src/Words.cpp
    src/Navigation.h src/Navigation.cpp src/Widths.h src/Widths.cpp
    src/Entries.h src/Entries.cpp
    # `SeriesCaptions` speaks for the universe block as well, so it carries that block with it
    # wherever it is built: a caption bound to two models links against both.
    src/SeriesCaptions.h src/SeriesCaptions.cpp
    src/Elsewhere.h src/Elsewhere.cpp)
target_include_directories(holds-the-volumes PRIVATE src)
target_link_libraries(holds-the-volumes PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network Qt6::Test ${QTKEYCHAIN_LIBRARIES})
add_test(NAME holds-the-volumes COMMAND holds-the-volumes)

# Where to go from a series that is not this series — and the ways through a universe, which
# are steps of works and not a list of editions sorted differently.
qt_add_executable(holds-elsewhere
    tests/holds_elsewhere.cpp tests/Pretend.h
    src/Settings.h src/Settings.cpp src/Server.h src/Server.cpp
    src/Api.h src/Api.cpp src/Words.h src/Words.cpp
    src/Navigation.h src/Navigation.cpp src/Widths.h src/Widths.cpp
    src/Entries.h src/Entries.cpp src/SeriesCaptions.h src/SeriesCaptions.cpp
    src/Elsewhere.h src/Elsewhere.cpp)
target_include_directories(holds-elsewhere PRIVATE src)
target_link_libraries(holds-elsewhere PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network Qt6::Test ${QTKEYCHAIN_LIBRARIES})
add_test(NAME holds-elsewhere COMMAND holds-elsewhere)

# What a deletion would take, asked before it is done. The one path in this client that
# unmakes a file, so every branch of it is walked here rather than on a screen.
qt_add_executable(weighs-a-deletion
    tests/weighs_a_deletion.cpp tests/Pretend.h
    src/Settings.h src/Settings.cpp src/Server.h src/Server.cpp
    src/Api.h src/Api.cpp src/Words.h src/Words.cpp
    src/Navigation.h src/Navigation.cpp src/Widths.h src/Widths.cpp
    src/Erasure.h src/Erasure.cpp)
target_include_directories(weighs-a-deletion PRIVATE src)
target_link_libraries(weighs-a-deletion PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network Qt6::Test ${QTKEYCHAIN_LIBRARIES})
add_test(NAME weighs-a-deletion COMMAND weighs-a-deletion)

# What the three dots do: the marks, and the copy of an archive written to disk.
qt_add_executable(commands-the-library
    tests/commands_the_library.cpp tests/Pretend.h
    src/Settings.h src/Settings.cpp src/Server.h src/Server.cpp
    src/Api.h src/Api.cpp src/Words.h src/Words.cpp
    src/Navigation.h src/Navigation.cpp src/Widths.h src/Widths.cpp
    src/Commands.h src/Commands.cpp)
target_include_directories(commands-the-library PRIVATE src)
target_link_libraries(commands-the-library PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network Qt6::Test ${QTKEYCHAIN_LIBRARIES})
add_test(NAME commands-the-library COMMAND commands-the-library)

# The two axes the bandeau offers, and the rule that decides whether an axis exists at all.
# Headless like the shelf: a filter that is not offered is a decision, not a drawing.
# The decision behind a page replacing another: two lists of names, no Qt models, no server.
qt_add_executable(holds-the-order
    tests/holds_the_order.cpp
    src/Rearrange.h src/Rearrange.cpp)
target_include_directories(holds-the-order PRIVATE src)
target_link_libraries(holds-the-order PRIVATE Qt6::Core Qt6::Test)
add_test(NAME holds-the-order COMMAND holds-the-order)

# What the settings screen knows. Both models outlive any screen, so both are headless.
qt_add_executable(holds-the-server-state
    tests/holds_the_server_state.cpp tests/Pretend.h
    src/Settings.h src/Settings.cpp src/Server.h src/Server.cpp
    src/Api.h src/Api.cpp src/Words.h src/Words.cpp
    src/Navigation.h src/Navigation.cpp src/Widths.h src/Widths.cpp
    src/Health.h src/Health.cpp src/Scan.h src/Scan.cpp)
target_include_directories(holds-the-server-state PRIVATE src)
target_link_libraries(holds-the-server-state PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network Qt6::Test ${QTKEYCHAIN_LIBRARIES})
add_test(NAME holds-the-server-state COMMAND holds-the-server-state)

qt_add_executable(holds-the-filters
    tests/holds_the_filters.cpp tests/Pretend.h
    src/Settings.h src/Settings.cpp src/Server.h src/Server.cpp
    src/Api.h src/Api.cpp src/Words.h src/Words.cpp
    src/Navigation.h src/Navigation.cpp src/Widths.h src/Widths.cpp
    src/Rearrange.h src/Rearrange.cpp src/Shelf.h src/Shelf.cpp
    src/Filters.h src/Filters.cpp)
target_include_directories(holds-the-filters PRIVATE src)
target_link_libraries(holds-the-filters PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network Qt6::Test ${QTKEYCHAIN_LIBRARIES})
add_test(NAME holds-the-filters COMMAND holds-the-filters)

# The key on the covers QML fetches for itself. Its live half needs two servers at once — the
# one configured, and another that must never be told the key.
qt_add_executable(puts-the-key-where-it-belongs
    tests/puts_the_key_where_it_belongs.cpp tests/Pretend.h
    src/Settings.h src/Settings.cpp src/Words.h src/Words.cpp src/Server.h src/Server.cpp
    src/Covers.h src/Covers.cpp)
target_include_directories(puts-the-key-where-it-belongs PRIVATE src)
target_link_libraries(puts-the-key-where-it-belongs PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network Qt6::Test ${QTKEYCHAIN_LIBRARIES})
add_test(NAME puts-the-key-where-it-belongs COMMAND puts-the-key-where-it-belongs)

qt_add_executable(against-the-real-server EXCLUDE_FROM_ALL
    tests/against_real.cpp
    src/Settings.h src/Settings.cpp src/Words.h src/Words.cpp src/Server.h src/Server.cpp)
target_include_directories(against-the-real-server PRIVATE src)
target_link_libraries(against-the-real-server PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network Qt6::Test ${QTKEYCHAIN_LIBRARIES})

# The three Python guards. They run here as well as in continuous integration, because a check
# that only ever runs on a server is a check nobody notices going quiet.
add_test(NAME knows-the-contract
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/client_knows_the_contract.py)

add_test(NAME bytes-stay-utf8
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/bytes_stay_utf8.py)

add_test(NAME nothing-talks-to-the-console
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/nothing_talks_to_the_console.py)

add_test(NAME words-stay-french
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/words_stay_french.py)

# These tests never need a scene graph, so the software-only offscreen platform keeps them
# independent of a desktop session.
set_tests_properties(keeps-the-key talks-to-the-server keeps-its-contrast embeds-its-fonts
    holds-a-shelf holds-the-search holds-the-resume holds-the-filters
    puts-the-key-where-it-belongs
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The seam test verifies the rounded shader one pixel at a time. It therefore needs the real
# X11 scene graph; CI supplies its display with Xvfb.
set_tests_properties(crosses-the-seam
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=xcb")

install(TARGETS leaf-desktop
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../assets/logos/leaf-dark.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME leaf.svg
)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/leaf.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
