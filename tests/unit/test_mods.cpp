// Camada de mods (runtime/mods/mod_api): replace() troca a funcao nas chamadas diretas (slot,
// como o codigo gerado com BS_MODS chama) e nas indiretas (despacho); call_original chama a
// gerada; reset() desfaz; BS_MOD registra e init_all() executa.
#include "test.h"

#include "cpu/sh4_runtime.h"
#include "mods/mod_api.h"

using namespace bs;

namespace {

int  g_orig = 0, g_new = 0;
bool g_modRan = false;

void origA(Sh4Context* c) {
    ++g_orig;
    c->r[0] = 1;
}
void newA(Sh4Context* c) {
    ++g_new;
    mods::call_original(0x8C010000, c);  // reaproveita a original e muda o retorno
    c->r[0] += 10;
}

Sh4Func                   slotA = &origA;  // como o "slot_8C010000" do dispatch.cpp gerado
const Sh4FuncEntry        kTable[] = {{0x0C010000, &origA}};
const mods::Slot          kSlots[] = {{0x8C010000, &slotA, &origA}};

} // namespace

BS_MOD(teste_registro) { g_modRan = true; }

BS_TEST(mods_replace_direct_and_indirect_calls) {
    sh4_set_function_table(kTable, 1);
    mods::set_slots(kSlots, 1);
    Sh4Context c{};
    BS_CHECK(mods::replace(0x8C010000, &newA));
    BS_CHECK(slotA == &newA && sh4_lookup(0x8C010000) == &newA && sh4_lookup(0xAC010000) == &newA);
    g_orig = g_new = 0;
    slotA(&c);  // chamada direta pelo slot
    BS_CHECK(g_new == 1 && g_orig == 1 && c.r[0] == 11);
    sh4_dispatch(&c, 0x8C020000, 0x8C010000);  // chamada indireta
    BS_CHECK(g_new == 2 && c.r[0] == 11);
    BS_CHECK(mods::original(0x8C010000) == &origA && !mods::replace(0x8C0FFFF0, &newA));
    mods::reset();
    BS_CHECK(slotA == &origA && sh4_lookup(0x8C010000) == &origA);
    // registro: o BS_MOD deste arquivo esta na lista e init_all o executa
    bool found = false;
    for (const auto& n : mods::registered()) found |= n == "teste_registro";
    g_modRan = false;
    mods::init_all();
    BS_CHECK(found && g_modRan);
    sh4_set_function_table(nullptr, 0);
    mods::set_slots(nullptr, 0);
}

namespace {
void otherA(Sh4Context* c) { c->r[0] = 99; }
} // namespace

// Substituicoes seguidas: a ultima vale; replace(addr, nullptr) volta ao original; sem slots
// (build sem BS_MODS), so as chamadas indiretas mudam e o slot nao e tocado.
BS_TEST(mods_replace_order_restore_and_no_slots) {
    sh4_set_function_table(kTable, 1);
    mods::set_slots(kSlots, 1);
    Sh4Context c{};
    BS_CHECK(mods::replace(0x8C010000, &newA) && mods::replace(0x8C010000, &otherA));
    slotA(&c);
    BS_CHECK(c.r[0] == 99 && sh4_lookup(0x8C010000) == &otherA);
    BS_CHECK(mods::replace(0x8C010000, nullptr));  // volta ao original
    BS_CHECK(slotA == &origA && sh4_lookup(0x8C010000) == &origA);

    mods::set_slots(nullptr, 0);  // como um build sem BS_MODS
    BS_CHECK(mods::replace(0x8C010000, &otherA));
    BS_CHECK(sh4_lookup(0x8C010000) == &otherA && slotA == &origA);
    BS_CHECK(mods::original(0x8C010000) == nullptr);  // sem slots nao ha "original" para chamar
    sh4_override(0x8C010000, nullptr);
    sh4_set_function_table(nullptr, 0);

    // endereco sem slot (HLE): reset() tambem desfaz a substituicao (revisao Codex 2026-10-15)
    sh4_register_hle(0x8C0000B0, &origA);
    BS_CHECK(mods::replace(0x8C0000B0, &otherA) && sh4_lookup(0x8C0000B0) == &otherA);
    mods::reset();
    BS_CHECK(sh4_lookup(0x8C0000B0) == &origA);
}
