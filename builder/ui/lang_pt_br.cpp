// Portugues (Brasil): idioma de referencia (todas as chaves).
#include "ui/i18n.h"

namespace bs::builder::ui {

namespace {
const Entry kEntries[] = {
    {"app.title", L"Blue Stinger Recompiler"},
    {"app.intro", L"Gera o Blue Stinger para Windows a partir da sua cópia do jogo (imagem CDI)."},
    {"lang.label", L"Idioma:"},

    {"disc.label", L"Imagem do jogo (CDI do Blue Stinger NTSC-U, sua cópia):"},
    {"out.label", L"Pasta de instalação (o jogo vai para a subpasta BlueStinger):"},
    {"browse", L"Procurar..."},
    {"portable.check", L"Modo portátil"},
    {"portable.off", L"As partidas ficam na pasta Documentos\\DC-Recomp\\Blue Stinger\\saves deste usuário."},
    {"portable.on", L"As partidas ficam na pasta saves dentro da instalação: o jogo pode ser levado para outra unidade ou outro computador."},

    {"btn.build", L"Recompilar"},
    {"btn.cancel", L"Cancelar"},
    {"btn.open", L"Abrir pasta"},
    {"btn.play", L"Jogar"},
    {"btn.help", L"Ajuda"},

    {"status.idle", L"Escolha a imagem do jogo e a pasta de instalação."},
    {"status.progress", L"{0}% - {1}"},
    {"status.cancelling", L"Cancelando..."},
    {"status.done", L"Pronto: o jogo está instalado em {0}."},
    {"status.failed", L"Não foi possível concluir a etapa \"{0}\"."},
    {"status.cancelled", L"Cancelado. A instalação anterior foi mantida."},

    {"step.validate", L"Conferindo as escolhas"},
    {"step.toolchain", L"Procurando o compilador"},
    {"step.extract", L"Preparando os dados do jogo"},
    {"step.recompile", L"Recompilando o código do jogo"},
    {"step.configure", L"Configurando a compilação"},
    {"step.compile", L"Compilando"},
    {"step.package", L"Montando o pacote"},
    {"step.verify", L"Testando o jogo compilado"},
    {"step.export", L"Exportando o projeto"},
    {"step.install", L"Instalando"},
    {"step.done", L"Concluído"},

    {"dlg.disc_title", L"Imagem CDI do Blue Stinger (NTSC-U)"},
    {"dlg.cdi_filter", L"Imagem de disco CDI (*.cdi)"},
    {"dlg.all_files", L"Todos os arquivos"},
    {"dlg.out_title", L"Pasta onde o jogo será instalado"},

    {"msg.missing_input", L"Escolha a imagem do jogo e a pasta de instalação."},
    {"msg.success", L"O Blue Stinger foi recompilado, testado e instalado em:\n{0}\n\nUse \"Jogar\" ou \"Abrir pasta\". O jogo contém código derivado da sua cópia: use-o só no seu computador e não o publique."},
    {"msg.previous_not_archived", L"\n\nAtenção: a versão anterior não pôde ser guardada como BlueStinger.anterior. Ela continua numa pasta ao lado da instalação; os detalhes estão em: {0}"},
    {"msg.close_running", L"A recompilação está em andamento. Cancelar e sair?"},
    {"msg.kit_missing", L"Faltam arquivos do recompilador (pasta kit). Extraia de novo a distribuição completa."},
    {"msg.existing", L"Já existe uma instalação em:\n{0}\n\nEla será atualizada. Saves, mods e outros arquivos seus são preservados, e a versão atual fica guardada em {1}.\n\nContinuar?"},
    {"msg.foreign", L"A pasta {0} já existe e não é uma instalação feita por este recompilador.\n\nNada é apagado: o conteúdo atual fica guardado em {1}, e os arquivos que não fazem parte do jogo continuam na nova instalação.\n\nContinuar?"},
    {"msg.mode_change", L"A instalação atual guarda as partidas em {0}. Depois desta recompilação elas ficarão em {1}.\n\nAs partidas existentes NÃO são transferidas: continuam onde estão e nada é apagado. Para continuar uma partida, copie os arquivos de saves de uma pasta para a outra.\n\nContinuar?"},
    {"loc.documents", L"Documentos\\DC-Recomp\\Blue Stinger\\saves"},
    {"loc.portable", L"saves, dentro da instalação"},

    {"err.footer", L"\n\nNenhuma instalação anterior foi alterada.\n\nDetalhes para diagnóstico: {0}"},
    {"err.invalid_disc", L"Esta imagem não pode ser usada. É preciso a imagem CDI do Blue Stinger para Dreamcast, versão americana (NTSC-U, T13001N V1.000)."},
    {"err.disc_not_found", L"A imagem do jogo não foi encontrada. Confira o arquivo escolhido."},
    {"err.no_space", L"Não há espaço livre suficiente na unidade da pasta de instalação. Libere espaço ou escolha outra pasta."},
    {"err.no_permission", L"Sem permissão para gravar na pasta escolhida. Escolha outra pasta, por exemplo dentro de Documentos."},
    {"err.path_too_long", L"O caminho da pasta de instalação é longo demais. Escolha uma pasta com caminho mais curto."},
    {"err.busy", L"Outra recompilação está usando esta pasta. Espere ela terminar e tente de novo."},
    {"err.toolchain", L"O compilador C++ do Visual Studio não foi encontrado. Instale o Visual Studio Build Tools com a carga \"Desenvolvimento para desktop com C++\" e tente de novo."},
    {"err.missing_input", L"Escolha a imagem do jogo e a pasta de instalação."},
    {"err.data", L"Os dados do jogo preparados nesta pasta estão incompletos ou alterados. Escolha a imagem do jogo para prepará-los de novo."},
    {"err.build", L"A compilação do jogo falhou."},
    {"err.verify", L"O jogo compilado não passou no teste de inicialização."},
    {"err.install", L"Não foi possível instalar a nova versão. O jogo está aberto? Feche-o e tente de novo. A versão anterior foi mantida."},
    {"err.other", L"Ocorreu um erro inesperado."},

    {"help.title", L"Ajuda"},
    {"help.body", L"1. Escolha a imagem CDI da sua cópia do Blue Stinger (Dreamcast, NTSC-U).\n2. Escolha a pasta de instalação. O jogo vai para a subpasta BlueStinger.\n3. Clique em Recompilar. A primeira vez leva alguns minutos; as seguintes reaproveitam o que já foi feito.\n4. No fim, use Jogar ou Abrir pasta.\n\nModo portátil: desmarcado, as partidas ficam em Documentos\\DC-Recomp\\Blue Stinger\\saves. Marcado, ficam na pasta saves da instalação, e o jogo pode ser levado para outra unidade. Trocar o modo não move as partidas existentes.\n\nRecompilar de novo na mesma pasta preserva saves e mods. O idioma escolhido aqui vale só para o recompilador, não para o jogo.\n\nTeclado: Tab passa de um campo ao outro; Enter recompila; Esc cancela; F1 abre esta ajuda."},
    {"tip.lang", L"Idioma desta janela. Não muda o idioma do jogo."},
    {"tip.disc", L"Arquivo .cdi da sua cópia do jogo."},
    {"tip.out", L"O jogo é instalado na subpasta BlueStinger desta pasta."},
    {"tip.portable", L"Guarda as partidas dentro da instalação em vez de Documentos."},
    {"tip.build", L"Recompila o jogo e instala na pasta escolhida."},
};
} // namespace

extern const Language kPtBr{"pt-BR", L"Português (Brasil)", kEntries};

} // namespace bs::builder::ui
