
install(FILES pinyin.txt DESTINATION share/deepin-anything-server)

# Install dconfig meta file
install(FILES ${CMAKE_SOURCE_DIR}/assets/org.deepin.anything.json DESTINATION share/dsg/configs/org.deepin.anything)
