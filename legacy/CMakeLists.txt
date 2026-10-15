# Simulacao do Dreamcast (runtime/: CPU SH-4, memoria, PowerVR, AICA, Maple, GD-ROM, BIOS
# HLE). No N0 os arquivos ficam onde estao; este alvo so nomeia o conjunto legado para que
# o produto e os testes declarem a dependencia. Cada subsistema sai daqui quando migrar.
add_library(bs_legacy INTERFACE)
target_link_libraries(bs_legacy INTERFACE bs_runtime)
