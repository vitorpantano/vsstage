#include <cstdio>
#include <string>
#include "core/Mapper.h"
using namespace vss;
static int fails = 0;
#define EXPECT_ROLE(n, r) do { auto got = mapper::roleOf(n); if (got != (r)) { std::printf("FAIL %s -> %s\n", n, roleInfo(got).id); ++fails; } } while (0)
int main() {
    EXPECT_ROLE("Click.wav", Role::Click); EXPECT_ROLE("Guia.wav", Role::Guide);
    EXPECT_ROLE("Bateria.wav", Role::Drums); EXPECT_ROLE("Baixo.wav", Role::Bass);
    EXPECT_ROLE("Violão.wav", Role::Acoustic); EXPECT_ROLE("Guitarra.wav", Role::Electric);
    EXPECT_ROLE("Teclado.wav", Role::Keys); EXPECT_ROLE("Pad.wav", Role::Pad);
    EXPECT_ROLE("Cordas.wav", Role::Strings); EXPECT_ROLE("Percussão.wav", Role::Perc);
    EXPECT_ROLE("Vocal Guia.wav", Role::Guide); EXPECT_ROLE("Track 01.wav", Role::Other);
    EXPECT_ROLE("Voz Lead.wav", Role::Vocal); EXPECT_ROLE("Back Vocal.wav", Role::Backing);
    EXPECT_ROLE("Loop.wav", Role::LoopFx);
    std::string t = mapper::titleFrom({"01 Deus Forte - Click.wav","01 Deus Forte - Bateria.wav"});
    if (t != "Deus Forte") { std::printf("FAIL title '%s'\n", t.c_str()); ++fails; }
    std::printf(fails ? "FAILED %d\n" : "mapper OK\n", fails);
    return fails ? 1 : 0;
}
