
install(TARGETS pulse-visualizer
  RUNTIME DESTINATION .
)

file(GLOB DLLS "${CMAKE_BINARY_DIR}/*.dll")
install(FILES ${DLLS}
  DESTINATION .
)

install(FILES README.md LICENSE CONTRIBUTORS
  DESTINATION .
)

install(DIRECTORY shaders/
  DESTINATION shaders/
  FILES_MATCHING PATTERN "*.vert" PATTERN "*.frag" PATTERN "*.comp"
)

# Install config template
install(FILES config.yml.template
  DESTINATION .
)

# Install font
install(FILES JetBrainsMonoNerdFont-Medium.ttf
  DESTINATION fonts/
)

# Install themes directory
install(DIRECTORY themes/
  DESTINATION themes/
  FILES_MATCHING PATTERN "*.txt"
)

set(CPACK_GENERATOR "NSIS")
set(CPACK_PACKAGE_NAME "pulse-visualizer")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_VENDOR "Audio-Solutions")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "pulse-visualizer")
set(CPACK_NSIS_MODIFY_PATH ON)
set(CPACK_NSIS_INSTALLED_ICON_NAME "${CMAKE_SOURCE_DIR}/media/icon/icon.ico")
set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/media/icon/icon-install.ico")
set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/media/icon/icon-uninstall.ico")
# set(CPACK_NSIS_HELP_LINK "https://github.com/help")
set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/Audio-Solutions/pulse-visualizer")
set(CPACK_NSIS_CONTACT "https://github.com/Audio-Solutions/pulse-visualizer/issues")
set(CPACK_NSIS_CREATE_ICONS_EXTRA
    "CreateShortCut '$SMPROGRAMS\\\\pulse-visualizer\\\\pulse-visualizer.lnk' '$INSTDIR\\\\pulse-visualizer.exe'"
)

set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

include(CPack)
