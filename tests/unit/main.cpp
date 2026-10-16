#include "test.h"

int main() {
    for (auto& c : bs::test::registry()) {
        int before = bs::test::failures();
        c.fn();
        std::printf("[%s] %s\n", bs::test::failures() == before ? " ok " : "FAIL", c.name);
        std::fflush(stdout);  // saida em arquivo: o ultimo teste fica visivel se outro travar
    }
    return bs::test::failures() ? 1 : 0;
}
