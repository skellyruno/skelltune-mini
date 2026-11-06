#include "ScaleMask.hpp"
#include "NoteDecision.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace skelly;
    static_assert(scaleMask(0,0)==0x0fff, "chromatic should allow all notes");
    static_assert(scaleMask(0,1)==0x0ab5, "C major");
    static_assert(scaleMask(0,2)==0x05ad, "C natural minor");
    static_assert(scaleMask(7,1)==0x0ad5, "G major");
    static_assert(effectiveMask(7,1,0)==scaleMask(7,1), "zero mask follows key and scale");
    static_assert(effectiveMask(7,1,0x012)==0x012, "manual mask overrides key and scale");
    static_assert(toggleNote(1u<<0, 0)==(1u<<0), "must keep at least one note enabled");
    static_assert(toggleNote(0x0fff, 11)==0x07ff, "toggle B off");
    static_assert(toggleNote(0x0002, 4)==0x0012, "toggle E on");
    for (int root=0;root<12;++root) {
        for (int scale=0;scale<3;++scale) {
            const auto mask = scaleMask(root,scale);
            assert(mask != 0);
            assert(mask < 4096);
            if (scale==0) assert(mask == 0x0fff);
            else {int count=0; for(int pc=0;pc<12;++pc) count += !!(mask & (1<<pc)); assert(count==7);}
        }
    }
    // Validate the controller reselects instantly when the user disables a held
    // pitch class, rather than continuing to tune against the forbidden note.
    skelly_research::NoteDecision controller;
    controller.prepare(48000.0);
    const auto first = controller.update(440.0, 0.99, 0.0, 64, 0x0fff, 440.0);
    assert(first.targetMidi == 69);
    const auto editedMask = std::uint16_t(0x0fff & ~(1u<<9)); // A is now forbidden
    const auto switched = controller.update(440.0, 0.99, 0.0, 64, editedMask, 440.0);
    assert(switched.changed && switched.targetMidi != 69 && !switched.pending);
    assert((editedMask & (1u << (switched.targetMidi%12))) != 0);
    std::cout << "v1.5 scale note-mask and immediate retarget tests passed\n";
}
