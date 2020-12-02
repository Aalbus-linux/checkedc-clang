include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/..)

add_clang_tool(clangd
  ClangdMain.cpp
  $<TARGET_OBJECTS:obj.clangDaemonTweaks>
  )

set(LLVM_LINK_COMPONENTS
  support
  )

set(CLANGD_XPC_LIBS "")
if(CLANGD_BUILD_XPC)
  list(APPEND CLANGD_XPC_LIBS "clangdXpcJsonConversions" "clangdXpcTransport")
endif()

clang_target_link_libraries(clangd
  PRIVATE
  clangAST
  clangBasic
  clangFormat
  clangFrontend
  clangLex
  clangSema
  clangTooling
  clangToolingCore
  clangToolingRefactoring
  clangToolingSyntax
  )
target_link_libraries(clangd
  PRIVATE
  clangTidy
  clangDaemon
  clangdSupport
  ${CLANGD_XPC_LIBS}
  )

add_clang_tool(icconv
        ClangdMain.cpp
        $<TARGET_OBJECTS:obj.clangDaemonTweaks>
        )

target_link_libraries(icconv
  PRIVATE
  clangAST
  clangBasic
  clangTidy
  cconvClangDaemon
  CConv
  clangFormat
  clangFrontend
  clangSema
  clangTooling
  clangToolingCore
  clangToolingSyntax
  ${CLANGD_XPC_LIBS}
  )

target_compile_definitions(icconv PUBLIC INTERACTIVECCCONV=1)
