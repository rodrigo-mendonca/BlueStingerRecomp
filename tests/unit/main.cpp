#include "test.h"

int main() {
    for (auto& c : bs::test::registry()) {
        int before = bs::test::failures();
        c.fn();
        std::printf("[%s] %s\n", bs::test::failures() == before ? " ok " : "FAIL", c.name);
    }
    return bs::test::failures() ? 1 : 0;
}
