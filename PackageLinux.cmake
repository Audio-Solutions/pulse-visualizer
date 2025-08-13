
install(TARGETS pulse-visualizer
  RUNTIME DESTINATION .
)

install(FILES README.md CONFIGURATION.md LICENSE CONTRIBUTORS
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
  DESTINATION .
)

# Install themes directory
install(DIRECTORY themes/
  DESTINATION themes/
  FILES_MATCHING PATTERN "*.txt"
)

# Create a desktop entry
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/pulse-visualizer.desktop.in"
  "${CMAKE_CURRENT_BINARY_DIR}/pulse-visualizer.desktop"
  @ONLY
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/pulse-visualizer.desktop"
  DESTINATION .
)

# Create a man page
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/pulse-visualizer.1.in"
  "${CMAKE_CURRENT_BINARY_DIR}/pulse-visualizer.1"
  @ONLY
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/pulse-visualizer.1"
  DESTINATION .
)

install(FILES media/icon/main.png
  DESTINATION .
  RENAME "pulse-visualizer.png"
)

install(PROGRAMS install.sh
  DESTINATION .
)

set(CPACK_GENERATOR "TGZ")
set(CPACK_PACKAGE_NAME "pulse-visualizer")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_VENDOR "Audio-Solutions")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "pulse-visualizer")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)

include(CPack)
