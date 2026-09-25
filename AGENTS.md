# TED emulation work

## Golden rule

Use only TED technical documentation to establish hardware behavior and design
emulation fixes. Do not consult, execute, compare against, or copy code from
other emulators, including YAPE and plus4emu. Do not use earlier observations
from those emulators as evidence for a change. Re-establish each hardware claim
from TED technical documentation; record uncertainties instead of guessing.

The user subsequently authorized research into FPGA reproductions of TED.
Consult their technical documentation and reports of measurements on original
hardware, as well as official Commodore documents and Plus/4 World technical
articles. Distinguish measured behavior, hypotheses, and FPGA implementation
choices. This does not authorize using YAPE or plus4emu, including indirectly
treating changes derived solely from those emulators as hardware evidence.

The user additionally authorized consulting reSID for performance ideas.
Its event scheduling and resampling techniques may inform optimization, but
SID hardware behavior is not evidence of TED behavior. The exclusion of YAPE
and plus4emu remains in force.

For the current Alpharay rendering investigation, the user explicitly
authorized a one-time exception to inspect YapeSDL source. Compare its TED
rendering and counter handling solely to diagnose the problem; do not copy
its code into VICE. Distinguish implementation choices from documented
hardware behavior. This exception does not authorize plus4emu
or unrelated emulator research.

On 2026-09-25 the user authorized analyzing the YapeSDL and plus4emu sources,
together with the TED technical documentation, to fix xplus4 emulation. This
supersedes the exclusions above for those two emulators. Where they agree with
each other and with the documentation, their behavior may guide a fix; record
disagreements and implementation choices instead of treating either emulator
as silicon. Reimplement behavior in VICE's style; do not paste their code.

Implement changes in the existing VICE coding style and architecture. Keep
fixes general to the emulated hardware, without game-specific workarounds.

Keep builds, downloaded documents, game/demo files, traces, and screenshots
outside this checkout. Include only source changes and relevant regression
tests needed for review.

Validate Pets Rescue using the user's disk in ~/Emulators/Plus4. Build the
default emulator set with SDL2, without GTK or the optional legacy x64.
Install validated builds under ~/.local so the home installation takes
precedence in PATH for both Plus/4 and VIC-20 projects.
