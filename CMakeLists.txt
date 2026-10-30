cmake_minimum_required(VERSION 3.14)
project(aethercli VERSION 0.7.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Opções de Build
option(ENABLE_ASAN "Enable Address Sanitizer (memory leaks)" OFF)
option(ENABLE_TSAN "Enable Thread Sanitizer (race conditions)" OFF)
option(ENABLE_DEBUG "Enable debug info and optimizations" OFF)
option(AETHERCLI_BUILD_TESTS "Build unit tests" ON)
option(AETHERCLI_ENABLE_MCP "Enable Model Context Protocol (MCP) server mode" OFF)

if(ENABLE_DEBUG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -g3")
endif()

if(ENABLE_ASAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer -g")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
endif()

if(ENABLE_TSAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread -g")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
endif()

include(FetchContent)

find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
  # JSON Library fallback to FetchContent using .tar.gz (avoids xz dependency on minimal systems)
  FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
    URL_HASH SHA256=0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406
  )
  set(JSON_BuildTests OFF CACHE INTERNAL "")
  FetchContent_MakeAvailable(json)
endif()

set(SOURCES
    src/main.cpp
)

if(AETHERCLI_ENABLE_MCP)
    list(APPEND SOURCES src/McpServer.cpp)
    add_compile_definitions(AETHERCLI_ENABLE_MCP=1)
endif()

include(GNUInstallDirs)

# Definir caminhos customizáveis
set(AETHERCLI_CONFIG_DIR "${CMAKE_INSTALL_FULL_SYSCONFDIR}/aethercli" CACHE PATH "Diretório de configurações")
set(AETHERCLI_CONFIG "${AETHERCLI_CONFIG_DIR}/config.json" CACHE FILEPATH "Arquivo de configuração principal")
set(AETHERCLI_VAR_DIR "${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/aethercli" CACHE PATH "Diretório de arquivos de estado/sessões")

add_executable(aethercli ${SOURCES})
target_compile_options(aethercli PRIVATE -Wall -Wextra)
target_link_libraries(aethercli PRIVATE
    nlohmann_json::nlohmann_json
)

# Passar caminhos e versão para o código-fonte
target_compile_definitions(aethercli PRIVATE
    AETHERCLI_CONFIG="${AETHERCLI_CONFIG}"
    AETHERCLI_VAR_DIR="${AETHERCLI_VAR_DIR}"
    AETHERCLI_VERSION="${PROJECT_VERSION}"
)

# Regras de instalação — permissões explícitas: config editável só pelo dono
# (quem edita config.json define o que o shell executa).
install(TARGETS aethercli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY etc/ DESTINATION ${AETHERCLI_CONFIG_DIR}
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
    DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                          GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    PATTERN "scripts" EXCLUDE
    PATTERN "users.json" EXCLUDE
    PATTERN ".env" EXCLUDE
    PATTERN ".env.*" EXCLUDE
)
install(DIRECTORY etc/scripts/ DESTINATION ${AETHERCLI_CONFIG_DIR}/scripts
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                          GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)
install(FILES man/man1/aethercli.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
install(FILES man/pt/man1/aethercli.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/pt/man1)
install(FILES man/pt/man1/aethercli.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/pt_BR/man1)

# Testes unitários (Catch2)
if(AETHERCLI_BUILD_TESTS)
    FetchContent_Declare(
      catch2
      URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.5.4.tar.gz
      URL_HASH SHA256=b7754b711242c167d8f60b890695347f90a1ebc95949a045385114165d606dbb
    )
    FetchContent_MakeAvailable(catch2)

    enable_testing()
    add_executable(aethercli_tests
        tests/test_cliutils.cpp
        tests/test_completer.cpp
        tests/test_parser.cpp
        tests/test_macro.cpp
        tests/test_auth.cpp
    )
    target_include_directories(aethercli_tests PRIVATE src)
    target_compile_options(aethercli_tests PRIVATE -Wall -Wextra)
    target_link_libraries(aethercli_tests PRIVATE
        Catch2::Catch2WithMain
        nlohmann_json::nlohmann_json
    )
    add_test(NAME unit_tests COMMAND aethercli_tests)
    add_test(NAME security_integration
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_security_integration.sh $<TARGET_FILE:aethercli>)
    if(AETHERCLI_ENABLE_MCP)
        add_test(NAME mcp_conformance
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mcp.sh $<TARGET_FILE:aethercli>)
    endif()
    find_program(EXPECT_BIN expect)
    if(EXPECT_BIN)
        add_test(NAME interactive_rawmode
            COMMAND ${EXPECT_BIN} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_interactive.exp $<TARGET_FILE:aethercli>)
    endif()
endif()
