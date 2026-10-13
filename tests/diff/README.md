# tests/diff/

Testes diferenciais: cada função recompilada roda a partir de um estado
inicial (registradores + RAM), e o resultado é comparado com o mesmo trecho
executado no Flycast. O Flycast roda como **processo externo**, controlado
pelo GDB stub. Ele nunca é linkado ao projeto, para manter a GPL fora do
código. Ver ARCHITECTURE.md §7.
