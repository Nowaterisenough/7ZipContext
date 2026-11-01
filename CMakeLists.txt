cmake_minimum_required(VERSION 3.20)
project(7ZipContext LANGUAGES C CXX)
enable_language(RC)
include(cmake/AppVersion.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# 7-Zip source directories
set(7Z_C_DIR ${CMAKE_SOURCE_DIR}/3rd/7zip/C)
set(7Z_CPP_DIR ${CMAKE_SOURCE_DIR}/3rd/7zip/CPP)

#############################################################################
# 7-Zip C Layer Sources
#############################################################################
set(7Z_C_SOURCES
    ${7Z_C_DIR}/Alloc.c
    ${7Z_C_DIR}/CpuArch.c
    ${7Z_C_DIR}/Sort.c
    ${7Z_C_DIR}/Threads.c
    ${7Z_C_DIR}/7zCrc.c
    ${7Z_C_DIR}/7zCrcOpt.c
    ${7Z_C_DIR}/XzCrc64.c
    ${7Z_C_DIR}/XzCrc64Opt.c
    ${7Z_C_DIR}/LzmaDec.c
    ${7Z_C_DIR}/LzmaEnc.c
    ${7Z_C_DIR}/Lzma2Dec.c
    ${7Z_C_DIR}/Lzma2DecMt.c
    ${7Z_C_DIR}/Lzma2Enc.c
    ${7Z_C_DIR}/LzFind.c
    ${7Z_C_DIR}/LzFindMt.c
    ${7Z_C_DIR}/LzFindOpt.c
    ${7Z_C_DIR}/Xz.c
    ${7Z_C_DIR}/XzDec.c
    ${7Z_C_DIR}/XzEnc.c
    ${7Z_C_DIR}/XzIn.c
    ${7Z_C_DIR}/Bra.c
    ${7Z_C_DIR}/Bra86.c
    ${7Z_C_DIR}/BraIA64.c
    ${7Z_C_DIR}/Bcj2.c
    ${7Z_C_DIR}/Bcj2Enc.c
    ${7Z_C_DIR}/Delta.c
    ${7Z_C_DIR}/Ppmd7.c
    ${7Z_C_DIR}/Ppmd7Dec.c
    ${7Z_C_DIR}/Ppmd7Enc.c
    ${7Z_C_DIR}/Ppmd7aDec.c
    ${7Z_C_DIR}/Ppmd8.c
    ${7Z_C_DIR}/Ppmd8Dec.c
    ${7Z_C_DIR}/Ppmd8Enc.c
    ${7Z_C_DIR}/Aes.c
    ${7Z_C_DIR}/AesOpt.c
    ${7Z_C_DIR}/Sha1.c
    ${7Z_C_DIR}/Sha1Opt.c
    ${7Z_C_DIR}/Sha256.c
    ${7Z_C_DIR}/Sha256Opt.c
    ${7Z_C_DIR}/7zAlloc.c
    ${7Z_C_DIR}/7zArcIn.c
    ${7Z_C_DIR}/7zBuf.c
    ${7Z_C_DIR}/7zBuf2.c
    ${7Z_C_DIR}/7zDec.c
    ${7Z_C_DIR}/7zFile.c
    ${7Z_C_DIR}/7zStream.c
    ${7Z_C_DIR}/BwtSort.c
    ${7Z_C_DIR}/HuffEnc.c
    ${7Z_C_DIR}/MtCoder.c
    ${7Z_C_DIR}/MtDec.c
    ${7Z_C_DIR}/Blake2s.c
    ${7Z_C_DIR}/SwapBytes.c
    ${7Z_C_DIR}/DllSecur.c
    ${7Z_C_DIR}/ZstdDec.c
    ${7Z_C_DIR}/Xxh64.c
)

#############################################################################
# 7-Zip CPP Windows Layer
#############################################################################
set(7Z_WINDOWS_SOURCES
    ${7Z_CPP_DIR}/Windows/FileDir.cpp
    ${7Z_CPP_DIR}/Windows/FileFind.cpp
    ${7Z_CPP_DIR}/Windows/FileIO.cpp
    ${7Z_CPP_DIR}/Windows/FileLink.cpp
    ${7Z_CPP_DIR}/Windows/FileName.cpp
    ${7Z_CPP_DIR}/Windows/PropVariant.cpp
    ${7Z_CPP_DIR}/Windows/PropVariantConv.cpp
    ${7Z_CPP_DIR}/Windows/PropVariantUtils.cpp
    ${7Z_CPP_DIR}/Windows/Synchronization.cpp
    ${7Z_CPP_DIR}/Windows/System.cpp
    ${7Z_CPP_DIR}/Windows/TimeUtils.cpp
)

#############################################################################
# 7-Zip CPP Common Layer
#############################################################################
set(7Z_CPP_COMMON_SOURCES
    ${7Z_CPP_DIR}/Common/CRC.cpp
    ${7Z_CPP_DIR}/Common/CrcReg.cpp
    ${7Z_CPP_DIR}/Common/IntToString.cpp
    ${7Z_CPP_DIR}/Common/MyString.cpp
    ${7Z_CPP_DIR}/Common/MyVector.cpp
    ${7Z_CPP_DIR}/Common/NewHandler.cpp
    ${7Z_CPP_DIR}/Common/Sha1Reg.cpp
    ${7Z_CPP_DIR}/Common/Sha256Reg.cpp
    ${7Z_CPP_DIR}/Common/StringConvert.cpp
    ${7Z_CPP_DIR}/Common/StringToInt.cpp
    ${7Z_CPP_DIR}/Common/UTFConvert.cpp
    ${7Z_CPP_DIR}/Common/Wildcard.cpp
    ${7Z_CPP_DIR}/Common/XzCrc64Reg.cpp
)

#############################################################################
# 7-Zip Common Module
#############################################################################
set(7Z_7ZIP_COMMON_SOURCES
    ${7Z_CPP_DIR}/7zip/Common/CreateCoder.cpp
    ${7Z_CPP_DIR}/7zip/Common/CWrappers.cpp
    ${7Z_CPP_DIR}/7zip/Common/FilePathAutoRename.cpp
    ${7Z_CPP_DIR}/7zip/Common/FileStreams.cpp
    ${7Z_CPP_DIR}/7zip/Common/FilterCoder.cpp
    ${7Z_CPP_DIR}/7zip/Common/InBuffer.cpp
    ${7Z_CPP_DIR}/7zip/Common/InOutTempBuffer.cpp
    ${7Z_CPP_DIR}/7zip/Common/LimitedStreams.cpp
    ${7Z_CPP_DIR}/7zip/Common/MemBlocks.cpp
    ${7Z_CPP_DIR}/7zip/Common/MethodId.cpp
    ${7Z_CPP_DIR}/7zip/Common/MethodProps.cpp
    ${7Z_CPP_DIR}/7zip/Common/OffsetStream.cpp
    ${7Z_CPP_DIR}/7zip/Common/OutBuffer.cpp
    ${7Z_CPP_DIR}/7zip/Common/OutMemStream.cpp
    ${7Z_CPP_DIR}/7zip/Common/ProgressMt.cpp
    ${7Z_CPP_DIR}/7zip/Common/ProgressUtils.cpp
    ${7Z_CPP_DIR}/7zip/Common/PropId.cpp
    ${7Z_CPP_DIR}/7zip/Common/StreamBinder.cpp
    ${7Z_CPP_DIR}/7zip/Common/StreamObjects.cpp
    ${7Z_CPP_DIR}/7zip/Common/StreamUtils.cpp
    ${7Z_CPP_DIR}/7zip/Common/UniqBlocks.cpp
    ${7Z_CPP_DIR}/7zip/Common/VirtThread.cpp
)

#############################################################################
# 7-Zip Compress Codecs
#############################################################################
set(7Z_COMPRESS_SOURCES
    ${7Z_CPP_DIR}/7zip/Compress/BZip2Crc.cpp
    ${7Z_CPP_DIR}/7zip/Compress/BZip2Decoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/BZip2Encoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/BZip2Register.cpp
    ${7Z_CPP_DIR}/7zip/Compress/Bcj2Coder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/Bcj2Register.cpp
    ${7Z_CPP_DIR}/7zip/Compress/BcjCoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/BcjRegister.cpp
    ${7Z_CPP_DIR}/7zip/Compress/BitlDecoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/BranchMisc.cpp
    ${7Z_CPP_DIR}/7zip/Compress/BranchRegister.cpp
    ${7Z_CPP_DIR}/7zip/Compress/ByteSwap.cpp
    ${7Z_CPP_DIR}/7zip/Compress/CopyCoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/CopyRegister.cpp
    ${7Z_CPP_DIR}/7zip/Compress/DeflateDecoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/DeflateEncoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/DeflateRegister.cpp
    ${7Z_CPP_DIR}/7zip/Compress/DeltaFilter.cpp
    ${7Z_CPP_DIR}/7zip/Compress/ImplodeDecoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/ImplodeHuffmanDecoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/LzmaDecoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/LzmaEncoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/LzmaRegister.cpp
    ${7Z_CPP_DIR}/7zip/Compress/Lzma2Decoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/Lzma2Encoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/Lzma2Register.cpp
    ${7Z_CPP_DIR}/7zip/Compress/LzOutWindow.cpp
    ${7Z_CPP_DIR}/7zip/Compress/LzxDecoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/PpmdDecoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/PpmdEncoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/PpmdRegister.cpp
    ${7Z_CPP_DIR}/7zip/Compress/PpmdZip.cpp
    ${7Z_CPP_DIR}/7zip/Compress/QuantumDecoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/ShrinkDecoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/XzDecoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/XzEncoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/ZDecoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/ZlibDecoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/ZlibEncoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/Rar1Decoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/Rar2Decoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/Rar3Decoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/Rar3Vm.cpp
    ${7Z_CPP_DIR}/7zip/Compress/Rar5Decoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/RarCodecsRegister.cpp
    ${7Z_CPP_DIR}/7zip/Compress/ZstdDecoder.cpp
    ${7Z_CPP_DIR}/7zip/Compress/LzfseDecoder.cpp
)

#############################################################################
# 7-Zip Crypto
#############################################################################
set(7Z_CRYPTO_SOURCES
    ${7Z_CPP_DIR}/7zip/Crypto/7zAes.cpp
    ${7Z_CPP_DIR}/7zip/Crypto/7zAesRegister.cpp
    ${7Z_CPP_DIR}/7zip/Crypto/HmacSha1.cpp
    ${7Z_CPP_DIR}/7zip/Crypto/HmacSha256.cpp
    ${7Z_CPP_DIR}/7zip/Crypto/MyAes.cpp
    ${7Z_CPP_DIR}/7zip/Crypto/MyAesReg.cpp
    ${7Z_CPP_DIR}/7zip/Crypto/Pbkdf2HmacSha1.cpp
    ${7Z_CPP_DIR}/7zip/Crypto/RandGen.cpp
    ${7Z_CPP_DIR}/7zip/Crypto/Rar20Crypto.cpp
    ${7Z_CPP_DIR}/7zip/Crypto/Rar5Aes.cpp
    ${7Z_CPP_DIR}/7zip/Crypto/RarAes.cpp
    ${7Z_CPP_DIR}/7zip/Crypto/WzAes.cpp
    ${7Z_CPP_DIR}/7zip/Crypto/ZipCrypto.cpp
    ${7Z_CPP_DIR}/7zip/Crypto/ZipStrong.cpp
)

#############################################################################
# 7-Zip Archive Handlers
#############################################################################
file(GLOB 7Z_ARCHIVE_COMMON_SOURCES
    ${7Z_CPP_DIR}/7zip/Archive/Common/*.cpp
)

file(GLOB 7Z_ARCHIVE_7Z_SOURCES
    ${7Z_CPP_DIR}/7zip/Archive/7z/*.cpp
)

file(GLOB 7Z_ARCHIVE_ZIP_SOURCES
    ${7Z_CPP_DIR}/7zip/Archive/Zip/*.cpp
)

file(GLOB 7Z_ARCHIVE_RAR_SOURCES
    ${7Z_CPP_DIR}/7zip/Archive/Rar/*.cpp
)

file(GLOB 7Z_ARCHIVE_TAR_SOURCES
    ${7Z_CPP_DIR}/7zip/Archive/Tar/*.cpp
)

file(GLOB 7Z_ARCHIVE_ISO_SOURCES
    ${7Z_CPP_DIR}/7zip/Archive/Iso/*.cpp
)

file(GLOB 7Z_ARCHIVE_CAB_SOURCES
    ${7Z_CPP_DIR}/7zip/Archive/Cab/*.cpp
)

# Main archive files (NOT including DllExports2.cpp which conflicts with our DllMain)
set(7Z_ARCHIVE_MAIN_SOURCES
    ${7Z_CPP_DIR}/7zip/Archive/ArchiveExports.cpp
    ${7Z_CPP_DIR}/7zip/Archive/LzmaHandler.cpp
    ${7Z_CPP_DIR}/7zip/Archive/SplitHandler.cpp
    ${7Z_CPP_DIR}/7zip/Archive/XzHandler.cpp
    ${7Z_CPP_DIR}/7zip/Archive/GzHandler.cpp
    ${7Z_CPP_DIR}/7zip/Archive/Bz2Handler.cpp
)

set(7Z_ARCHIVE_SOURCES
    ${7Z_ARCHIVE_MAIN_SOURCES}
    ${7Z_ARCHIVE_COMMON_SOURCES}
    ${7Z_ARCHIVE_7Z_SOURCES}
    ${7Z_ARCHIVE_ZIP_SOURCES}
    ${7Z_ARCHIVE_RAR_SOURCES}
    ${7Z_ARCHIVE_TAR_SOURCES}
    ${7Z_ARCHIVE_ISO_SOURCES}
    ${7Z_ARCHIVE_CAB_SOURCES}
)
# Note: Wim format removed - requires Lzms, Xpress, Xml which add too many dependencies

#############################################################################
# 7-Zip UI Common (for TypeToProp, FlagsToString)
#############################################################################
set(7Z_UI_COMMON_SOURCES
    ${7Z_CPP_DIR}/7zip/UI/Common/PropIDUtils.cpp
)

#############################################################################
# Collect all 7-Zip sources
#############################################################################
set(7Z_ALL_SOURCES
    ${7Z_C_SOURCES}
    ${7Z_WINDOWS_SOURCES}
    ${7Z_CPP_COMMON_SOURCES}
    ${7Z_7ZIP_COMMON_SOURCES}
    ${7Z_COMPRESS_SOURCES}
    ${7Z_CRYPTO_SOURCES}
    ${7Z_ARCHIVE_SOURCES}
    ${7Z_UI_COMMON_SOURCES}
)

#############################################################################
# Project sources
#############################################################################
add_library(SevenZipEngine OBJECT
    src/SevenZipCore.cpp src/ArchivePlan.cpp src/GuidInit.cpp
    ${7Z_ALL_SOURCES}
)
add_library(7ZipContext SHARED src/ContextMenu.cpp src/ContextMenu.def)
set_target_properties(7ZipContext PROPERTIES PREFIX "")
find_package(Qt6 6.6 REQUIRED COMPONENTS Widgets Network Svg)
qt_add_library(ArchiveUi STATIC
    src/ui/ArchiveController.cpp src/ui/ArchiveController.h
    src/ui/ArchiveWindow.cpp src/ui/ArchiveWindow.h
    src/ui/PasswordLibrary.cpp src/ui/PasswordLibrary.h
    src/ui/ArchivePasswords.cpp src/ui/ArchivePasswords.h
    src/ui/NestedArchives.cpp src/ui/NestedArchives.h
    src/ui/ArchiveCleanup.cpp src/ui/ArchiveCleanup.h
    src/ui/MediaOrganizer.cpp src/ui/MediaOrganizer.h
    src/ui/InputDiscovery.cpp src/ui/InputDiscovery.h
    src/ui/UpdateManager.cpp src/ui/UpdateManager.h
    src/ui/theme/dark_style.cpp src/ui/theme/dark_style.h
)
set_target_properties(ArchiveUi PROPERTIES AUTOMOC ON)
target_include_directories(ArchiveUi PUBLIC src "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(ArchiveUi PUBLIC Qt6::Widgets Qt6::Svg Qt6::Network dwmapi crypt32 msi)
qt_add_executable(7ZipContextApp WIN32 src/DesktopApp.cpp "${CMAKE_BINARY_DIR}/generated/AppVersion.rc")
target_link_libraries(7ZipContextApp PRIVATE ArchiveUi SevenZipEngine Qt6::Network)

foreach(target SevenZipEngine 7ZipContext ArchiveUi 7ZipContextApp)
    target_include_directories(${target} PRIVATE src ${7Z_C_DIR} ${7Z_CPP_DIR})
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE WIN32 _WINDOWS NOMINMAX _7ZIP_ST Z7_PPMD_SUPPORT Z7_ZSTD_DISABLE)
    target_link_libraries(${target} PUBLIC shlwapi pathcch ole32 oleaut32 shell32 uuid advapi32 user32 gdi32 comctl32 uxtheme)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /W3 /wd4996 /wd4267 /wd4244 /wd4018 /wd4389 /wd4127 /wd4100)
    endif()
endforeach()
set(7Z_CPP_ALL_SOURCES ${7Z_ALL_SOURCES})
list(REMOVE_ITEM 7Z_CPP_ALL_SOURCES ${7Z_C_SOURCES})
if(MSVC)
    set_source_files_properties(${7Z_C_SOURCES} PROPERTIES COMPILE_FLAGS "/FI\"${7Z_C_DIR}/Precomp.h\"")
    set_source_files_properties(${7Z_CPP_ALL_SOURCES} PROPERTIES COMPILE_FLAGS "/FI\"${7Z_CPP_DIR}/Common/Common.h\"")
else()
    set_source_files_properties(${7Z_C_SOURCES} PROPERTIES COMPILE_OPTIONS "-include;${7Z_C_DIR}/Precomp.h")
    set_source_files_properties(${7Z_CPP_ALL_SOURCES} PROPERTIES COMPILE_OPTIONS "-include;${7Z_CPP_DIR}/Common/Common.h")
    target_link_options(7ZipContext PRIVATE -static)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(ArchiveTests tests/ArchiveTests.cpp)
    target_include_directories(ArchiveTests PRIVATE src)
    target_compile_definitions(ArchiveTests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(ArchiveTests PRIVATE SevenZipEngine)
    if(MSVC)
        target_compile_options(ArchiveTests PRIVATE /utf-8)
    else()
        target_link_options(ArchiveTests PRIVATE -static)
    endif()
    add_test(NAME archive_roundtrip COMMAND ArchiveTests)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(ArchiveUiTests tests/ArchiveUiTests.cpp)
    set_target_properties(ArchiveUiTests PROPERTIES AUTOMOC ON WIN32_EXECUTABLE FALSE)
    target_compile_definitions(ArchiveUiTests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(ArchiveUiTests PRIVATE ArchiveUi SevenZipEngine Qt6::Test)
    if(MSVC)
        target_compile_options(ArchiveUiTests PRIVATE /utf-8)
    endif()
    add_test(NAME archive_ui COMMAND ${CMAKE_COMMAND}
        -DTEST_EXECUTABLE=$<TARGET_FILE:ArchiveUiTests>
        -P ${CMAKE_SOURCE_DIR}/tests/RunUiTests.cmake)
    qt_add_executable(PasswordTests tests/PasswordTests.cpp)
    set_target_properties(PasswordTests PROPERTIES AUTOMOC ON WIN32_EXECUTABLE FALSE)
    target_compile_definitions(PasswordTests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(PasswordTests PRIVATE ArchiveUi SevenZipEngine Qt6::Test)
    if(MSVC)
        target_compile_options(PasswordTests PRIVATE /utf-8)
    endif()
    add_test(NAME password_lists COMMAND ${CMAKE_COMMAND}
        -DTEST_EXECUTABLE=$<TARGET_FILE:PasswordTests> -DTEST_LOG=password-tests.log
        -P ${CMAKE_SOURCE_DIR}/tests/RunUiTests.cmake)
    qt_add_executable(NestedArchiveTests tests/NestedArchiveTests.cpp)
    set_target_properties(NestedArchiveTests PROPERTIES AUTOMOC ON WIN32_EXECUTABLE FALSE)
    target_compile_definitions(NestedArchiveTests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(NestedArchiveTests PRIVATE ArchiveUi SevenZipEngine Qt6::Test)
    if(MSVC)
        target_compile_options(NestedArchiveTests PRIVATE /utf-8)
    endif()
    add_test(NAME nested_archives COMMAND ${CMAKE_COMMAND}
        -DTEST_EXECUTABLE=$<TARGET_FILE:NestedArchiveTests> -DTEST_LOG=nested-tests.log
        -P ${CMAKE_SOURCE_DIR}/tests/RunUiTests.cmake)
    qt_add_executable(MediaOrganizationTests tests/MediaOrganizationTests.cpp)
    set_target_properties(MediaOrganizationTests PROPERTIES AUTOMOC ON WIN32_EXECUTABLE FALSE)
    target_compile_definitions(MediaOrganizationTests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(MediaOrganizationTests PRIVATE ArchiveUi SevenZipEngine Qt6::Test)
    if(MSVC)
        target_compile_options(MediaOrganizationTests PRIVATE /utf-8)
    endif()
    add_test(NAME media_organization COMMAND ${CMAKE_COMMAND}
        -DTEST_EXECUTABLE=$<TARGET_FILE:MediaOrganizationTests> -DTEST_LOG=media-tests.log
        -P ${CMAKE_SOURCE_DIR}/tests/RunUiTests.cmake)
    qt_add_executable(InputDiscoveryTests tests/InputDiscoveryTests.cpp)
    set_target_properties(InputDiscoveryTests PROPERTIES AUTOMOC ON WIN32_EXECUTABLE FALSE)
    target_compile_definitions(InputDiscoveryTests PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(InputDiscoveryTests PRIVATE ArchiveUi SevenZipEngine Qt6::Test)
    if(MSVC)
        target_compile_options(InputDiscoveryTests PRIVATE /utf-8)
    endif()
    add_test(NAME mixed_input COMMAND ${CMAKE_COMMAND}
        -DTEST_EXECUTABLE=$<TARGET_FILE:InputDiscoveryTests> -DTEST_LOG=input-tests.log
        -P ${CMAKE_SOURCE_DIR}/tests/RunUiTests.cmake)
    qt_add_executable(UpdateTests tests/UpdateTests.cpp)
    set_target_properties(UpdateTests PROPERTIES AUTOMOC ON WIN32_EXECUTABLE FALSE)
    target_link_libraries(UpdateTests PRIVATE ArchiveUi SevenZipEngine Qt6::Test)
    if(MSVC)
        target_compile_options(UpdateTests PRIVATE /utf-8)
    endif()
    add_test(NAME online_updates COMMAND ${CMAKE_COMMAND}
        -DTEST_EXECUTABLE=$<TARGET_FILE:UpdateTests> -DTEST_LOG=update-tests.log
        -P ${CMAKE_SOURCE_DIR}/tests/RunUiTests.cmake)
    add_executable(UpdateSmokeFailure WIN32 tests/UpdateSmokeFailure.cpp "${CMAKE_BINARY_DIR}/generated/AppVersion.rc")
    if(MINGW)
        target_link_options(UpdateSmokeFailure PRIVATE -static)
    endif()
    add_executable(VisualReferenceProbe EXCLUDE_FROM_ALL tests/VisualReferenceProbe.cpp)
    target_include_directories(VisualReferenceProbe PRIVATE src)
    target_link_libraries(VisualReferenceProbe PRIVATE Qt6::Widgets)
    if(MSVC)
        target_compile_options(VisualReferenceProbe PRIVATE /utf-8)
    endif()
    add_executable(RenderUiReferences EXCLUDE_FROM_ALL tests/RenderUiReferences.cpp)
    target_compile_definitions(RenderUiReferences PRIVATE UNICODE _UNICODE NOMINMAX)
    target_link_libraries(RenderUiReferences PRIVATE ArchiveUi SevenZipEngine)
    if(MSVC)
        target_compile_options(RenderUiReferences PRIVATE /utf-8)
    endif()
endif()

set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Install path" FORCE)
install(TARGETS 7ZipContext 7ZipContextApp RUNTIME DESTINATION . LIBRARY DESTINATION .)
install(FILES scripts/install.ps1 scripts/uninstall.ps1 DESTINATION .)
install(FILES scripts/Apply-Update.ps1 DESTINATION .)
install(FILES Package/AppxManifest.xml DESTINATION .)
install(DIRECTORY Package/Assets DESTINATION .)
install(DIRECTORY licenses DESTINATION .)
qt_generate_deploy_app_script(TARGET 7ZipContextApp OUTPUT_SCRIPT qt_deploy_script NO_UNSUPPORTED_PLATFORM_ERROR)
install(CODE "
    set(QT_DEPLOY_BIN_DIR \".\")
    set(QT_DEPLOY_LIB_DIR \".\")
    set(QT_DEPLOY_PLUGINS_DIR \"plugins\")
    set(QT_DEPLOY_TRANSLATIONS_DIR \"translations\")
")
install(SCRIPT ${qt_deploy_script})
if(MINGW)
    get_target_property(_qt_core_location Qt6::Core IMPORTED_LOCATION_RELEASE)
    get_filename_component(_qt_bin_dir "${_qt_core_location}" DIRECTORY)
    get_filename_component(_compiler_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    install(CODE "
        set(RUNTIME_APPLICATION \"\${CMAKE_INSTALL_PREFIX}/7ZipContextApp.exe\")
        set(RUNTIME_QT_BIN_DIR \"${_qt_bin_dir}\")
        set(RUNTIME_COMPILER_BIN_DIR \"${_compiler_bin_dir}\")
        set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM windows+pe)
        set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL objdump)
        set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND \"${CMAKE_OBJDUMP}\")
        include(\"${CMAKE_SOURCE_DIR}/cmake/DeployWindowsRuntime.cmake\")
    ")
endif()
