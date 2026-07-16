cmake_minimum_required(VERSION 3.14)
project(aethercli VERSION 0.5.0)

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

# JSON Library (URL_HASH: integridade do download)
FetchContent_Declare(
  json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(json)

# cpp-httplib for AI API (HTTPLIB_INSTALL OFF: dependência interna não deve
# instalar headers/cmake em /usr/local junto com o aethercli)
set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  httplib
  URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.7.tar.gz
  URL_HASH SHA256=b7b1e9e4e77565a5a9bc95e761d5df3e7c0e8ca37c90fd78b1b031bc6cb90fc1
)
FetchContent_MakeAvailable(httplib)

set(SOURCES
    src/main.cpp
)

include(GNUInstallDirs)

# Definir caminhos customizáveis
set(AETHERCLI_CONFIG_DIR "${CMAKE_INSTALL_FULL_SYSCONFDIR}/aethercli" CACHE PATH "Diretório de configurações")
set(AETHERCLI_CONFIG "${AETHERCLI_CONFIG_DIR}/config.json" CACHE FILEPATH "Arquivo de configuração principal")
set(AETHERCLI_VAR_DIR "${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/aethercli" CACHE PATH "Diretório de arquivos de estado/sessões")

add_executable(aethercli ${SOURCES})
target_compile_options(aethercli PRIVATE -Wall -Wextra)
target_link_libraries(aethercli PRIVATE
    nlohmann_json::nlohmann_json
    httplib::httplib
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
)
install(DIRECTORY etc/scripts/ DESTINATION ${AETHERCLI_CONFIG_DIR}/scripts
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                          GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

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
endif()
