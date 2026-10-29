// N8.7 (docs/N8-7-KAMUI.md, D-K3 e incremento 4a): o banco minimo de status de interrupcao nativo
// (native/render/kamui/event_status): eventos entregues e levantados, limpeza por escrita de 1, mascaras e nivel IRL.
#include "test.h"

#include "render/kamui/event_status.h"

using bs::native::render::kamui::EventStatus;

// a sequencia do tratador 8C1C53E2 por render (fase 0): fim do DMA + fim do modificador opaco, limpos pelo tratador
BS_TEST(event_status_raise_and_write_one_to_clear) {
    EventStatus s;
    s.reset();
    s.raise((1u << 19) | (1u << 8) | (1u << 4));
    BS_CHECK(s.reg(EventStatus::kIstNrm) == 0x80110 && s.events() == 1);
    s.write(EventStatus::kIstNrm, (1u << 19) | (1u << 8));  // limpa so os bits em 1
    BS_CHECK(s.reg(EventStatus::kIstNrm) == 0x10);
    s.write(EventStatus::kIstErr, 0xFFFFFFFF);  // limpar o que nao esta aceso nao acende nada
    BS_CHECK(s.reg(EventStatus::kIstErr) == 0);
}

// VBlank-In entregue com o bit ainda aceso conta como fundido; fim de render acende 0-2 e o Maple, o 12
BS_TEST(event_status_deliver_counts_vblanks_and_fused) {
    EventStatus      s;
    EventStatus::Due d;
    s.reset();
    d.vblank_in = true;
    s.deliver(d);
    BS_CHECK(s.vblanks() == 1 && s.fused() == 0 && s.reg(EventStatus::kIstNrm) == EventStatus::kVblankIn);
    s.deliver(d);  // nao reconhecido: um atendimento para dois campos
    BS_CHECK(s.vblanks() == 2 && s.fused() == 1);
    EventStatus::Due r;
    r.render = r.maple = r.vblank_out = true;
    s.deliver(r);
    BS_CHECK(s.reg(EventStatus::kIstNrm) == (EventStatus::kVblankIn | EventStatus::kVblankOut | 0x7 | (1u << 12)));
    BS_CHECK(s.events() == 3);
    s.deliver(EventStatus::Due{});  // nada venceu: nao conta
    BS_CHECK(s.events() == 3);
    s.reset();  // as contagens de VBlank zeram; a de eventos e do processo
    BS_CHECK(s.vblanks() == 0 && s.fused() == 0 && s.events() == 3 && s.reg(EventStatus::kIstNrm) == 0);
}

// o nivel e o maior cujas mascaras (NRM, EXT, ERR) pegam um bit pendente
BS_TEST(event_status_irl_level_follows_the_masks) {
    EventStatus s;
    s.reset();
    s.raise(EventStatus::kVblankIn);
    BS_CHECK(s.irl_level() == 0);  // sem mascara
    s.write(0x005F6910, EventStatus::kVblankIn);  // IML2NRM
    BS_CHECK(s.irl_level() == 2);
    s.write(0x005F6920, EventStatus::kVblankIn);  // IML4NRM
    BS_CHECK(s.irl_level() == 4);
    s.write(0x005F6930, EventStatus::kVblankIn);  // IML6NRM
    BS_CHECK(s.irl_level() == 6);
    s.write(EventStatus::kIstNrm, EventStatus::kVblankIn);
    BS_CHECK(s.irl_level() == 0);
    s.write(0x005F6928, 1);  // IML4ERR
    BS_CHECK(s.irl_level() == 0 && s.reg(0x005F6928) == 1);
}
