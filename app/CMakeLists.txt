# Nucleo do executavel (sem janela): execucao do jogo numa thread, ritmo e quadros.
add_library(bs_app_core STATIC game_host.cpp assist.cpp
            subtitle_overlay.cpp   # N7c: legendas das falas por cima do gameplay
            settings_menu.cpp      # N7c: modelo do menu interno de configuracoes
            menu_overlay.cpp)      # N7c: desenho do menu (Direct2D/DirectWrite)
target_include_directories(bs_app_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bs_app_core PUBLIC bs_runtime bs_transitional bs_video_adapter bs_sound_adapter bs_native_localization
                      bs_native_video PRIVATE d2d1 dwrite windowscodecs ole32)

# Executavel nativo: so com o codigo gerado (do executavel do proprio usuario) e a pedido.
# O SDL3 (zlib) fica restrito a este alvo: o runtime e as ferramentas nao dependem dele.
if(BS_BUILD_GAME AND TARGET bs_generated)
  include(FetchContent)
  set(SDL_SHARED       OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC       ON  CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)
  # sem asserts internos: os SDL_assert_release gravavam __FILE__ (caminho absoluto do
  # build) no executavel distribuido (revisao Codex 2026-10-14)
  set(SDL_ASSERTIONS   "disabled" CACHE STRING "" FORCE)
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.30
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(SDL3)

  # Identificacao da compilacao: commit (com -dirty se houver mudancas) e data.
  # No kit do Builder nao ha .git: a versao vem de BS_VERSION_OVERRIDE (arquivo VERSION do kit).
  set(BS_VERSION_OVERRIDE "" CACHE STRING "Versao fixa (kit do Builder)")
  find_package(Git QUIET)
  set(BS_VERSION "desconhecida")
  if(BS_VERSION_OVERRIDE)
    set(BS_VERSION "${BS_VERSION_OVERRIDE}")
  elseif(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --always --dirty --abbrev=7
                    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE BS_VERSION
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  endif()
  string(TIMESTAMP BS_BUILD_DATE "%Y-%m-%d %H:%M")
  configure_file(bs_version.h.in ${CMAKE_CURRENT_BINARY_DIR}/bs_version.h @ONLY)

  # Subsistema grafico (sem janela de console); a entrada continua sendo main().
  add_executable(bluestinger WIN32 main_sdl.cpp window_present.cpp)  # N7c: apresentacao Vulkan direta ou SDL
  if(WIN32)
    # Icone do executavel: o original do projeto (app/res, desenho de tools/dev/gerar_icone.py) ou, se existir,
    # icone-local/bluestinger.ico, feito da capa do proprio usuario (gerar_icone.py --capa). Esse fica fora do git e
    # so entra em builds locais: e arte do jogo, nao se publica.
    set(BS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/res/bluestinger.ico")
    # (a escolha e na configuracao do CMake: por ou tirar o icone local vale na proxima configuracao)
    if(EXISTS "${CMAKE_SOURCE_DIR}/icone-local/bluestinger.ico")
      set(BS_ICON "${CMAKE_SOURCE_DIR}/icone-local/bluestinger.ico")
      message(STATUS "icone: icone-local/bluestinger.ico (capa do usuario; uso local, nao publicar)")
    endif()
    # o compilador de recursos nao acompanha o .ico: o hash dele no .rc gerado faz a troca do icone recompilar o recurso
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BS_ICON}")
    file(SHA256 "${BS_ICON}" BS_ICON_HASH)
    file(TO_CMAKE_PATH "${BS_ICON}" BS_ICON_RC)
    configure_file(bluestinger.rc.in ${CMAKE_CURRENT_BINARY_DIR}/bluestinger.rc @ONLY)
    target_sources(bluestinger PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/bluestinger.rc)
  endif()
  target_include_directories(bluestinger PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  target_link_libraries(bluestinger PRIVATE bs_app_core bs_generated SDL3::SDL3-static)
  # mods do projeto editavel: cada .cpp de BS_MODS_DIR entra no executavel (BS_MOD registra)
  if(BS_MODS AND BS_MODS_DIR)
    file(GLOB BS_MOD_SOURCES CONFIGURE_DEPENDS "${BS_MODS_DIR}/*.cpp")
    target_sources(bluestinger PRIVATE ${BS_MOD_SOURCES})
    message(STATUS "mods: ${BS_MOD_SOURCES}")
  endif()
  if(MSVC)
    # mapa do linker (so no diretorio de build; o Builder copia apenas o .exe): o teste
    # n0_produto_sem_cdi confere que a leitura de CDI nao entrou no produto
    target_link_options(bluestinger PRIVATE /ENTRY:mainCRTStartup "/MAP:$<TARGET_FILE_DIR:bluestinger>/bluestinger.map")
  endif()
endif()
