# Stallion: credits

Stallion is free firmware for the M-VAVE FM-1 and the M-VAVE SMK-37 Pro, by **Eberon Limited (John Gorman)**. It
exists because many people shared their code, their research and their time in the open. This page names the projects
and people whose work is in it, what they did, where to find it and under which licence. Every part is GPL-3.0-only
unless it says otherwise here or in [LICENSING.md](LICENSING.md), which lists the licences file by file
(`assets/MANIFEST` lists every data file). The names below are as the projects' own notices and pages give them.

## Stallion

- **Eberon Limited (John Gorman)**, [EberonLtd/stallion-fm1](https://github.com/EberonLtd/stallion-fm1)
  (GPL-3.0-only): the PALOMINO, STAMPEDE, RODEO and MARE engines, FILTER 2, SLOPE, the OTA model, the five INSERT types'
  ports, KEY TOUCH's adaptation, LEVER and RIBBON, the BEND page, the MOTION lanes and NOTE PRESS, the SMK-37 pad and
  key aftertouch, the matrix work, editor protocol 15, the longship installer, the update screen, the icon atlas,
  the logo and the fixes listed in the [README](README.md#bug-fixes).

## The projects Stallion is built on

| Project | Authors (as their notices say) | Licence | What is in Stallion |
| --- | --- | --- | --- |
| [Clipper](https://github.com/isod89/clipper-fm1) | 3dSam / isod89 | GPL-3.0-only | Stallion's upstream: forked at Clipper 1.1, Clipper 1.3 and 1.3.1 merged. HYBRID, FMX, VECTOR, WSEQ, MACRO, the filter models, TURNG, SPICE / DICE and most of the tree |
| [Clipper Pro](https://github.com/isod89/clipper-pro) | isod89 (Sodwave, 3dSam), with M. Geisler (genkigenki / makkuro) | GPL-3.0-only | The SMK-37 Pro port, merged as history (Clipper Pro 1.3): the board layer, the fader banks, the NOTES and SOUNDS pads, the installer page's SMK-37 support |
| [SLOOP](https://github.com/isod89/sloop-fm1) | isod89 (3dSam) | GPL-3.0-only | Everything Clipper starts from: the 12 sound engines, the effects, the arpeggiator, the step sequencer, USB audio and MIDI. SLOOP's contributors are credited in its README; its manuals are in [docs/sloop](docs/sloop/) |
| [Felucca](https://github.com/hugelton/Felucca) | Leo Kuroshita (@kurogedelic), Hügelton Instruments | GPL-3.0-only | The open FM-1 firmware under all of it: the platform, the engines, the editor, the M-UPGRADE-compatible installer and the update loader. Most of the code carries his copyright line |

## Code from other projects

| Project | Authors | Licence | What is in Stallion |
| --- | --- | --- | --- |
| [ESTAMPIE](https://github.com/ocean/estampie) | Drew Robinson | GPL-3.0-only | KEY TOUCH: velocity and pressure from the FM-1's own keys (`firmware/src/key_touch.c`, at af5a871), adapted to Stallion's settings |
| [Velo](https://github.com/charlesvestal/fm1-velocity) and [MONK-1](https://github.com/charlesvestal/fm1-monk) | Charles Vestal | GPL-3.0-only | The key sensing ESTAMPIE ported KEY TOUCH from |
| [X0X](https://github.com/charlesvestal/fm1-x0x) | Charles Vestal | GPL-3.0 | The second core's HAL (`firmware/hal/fm1_cpu1.h`, `fm1_cpu1.S`) and the supply levels for two cores, through SLOOP and Clipper |
| [Melodee](https://github.com/keremimo/melodee) | Kerem Kilic (keremimo) | credit (X0X's HAL came after it) | The dual-core start, with X0X |
| [sloop-lab](https://github.com/andrerezemini/sloop-lab) | Andre Rezemini | GPL-3.0 | The sound on the second core (`firmware/src/cpu1.c`, `cpu1.h`, `tests/cpu1_host.c`), through SLOOP 2.7 |
| [JIANT FM](https://github.com/juanjiant-bit/JIANT) | juanjiant-bit | credit | The knob acceleration and the LEVEL / PAN glide, through SLOOP 2.7 |
| [OberVave](https://github.com/zvenson/obervave) | zvenson | GPL-3.0-only | RODEO's oscillators, hard sync, X-MOD and state-variable filter, and its factory programs mapped to RODEO's presets (`firmware/src/eng_ober.c`) |
| [TripDeck](https://github.com/anonymousacademe/FM-1-TripDeck) | anonymousacademe | GPL-3.0-only | The pitch shifter of the INSERT's PITCH and PIT2 |
| [CS-1](https://github.com/twochannels/CS-1) | twochannels | GPL-3.0-only | The six-stage phaser of the INSERT's PHAS6 |
| [Airwindows](https://github.com/airwindows/airwindows) Pop3 | Chris Johnson | MIT | The compressor's law of the INSERT's LOCMP (`LICENSES/MIT-Airwindows.txt`) |
| [Lunar Modulator](https://github.com/ip2k/lunar-modulator) | ip2k | MIT | Its port of Pop3, which LOCMP follows; no code copied (`LICENSES/MIT-Lunar-Modulator.txt`) |
| [CrispyZebra](https://github.com/hugelton/CrispyZebra) | Leo Kuroshita | GPL-3.0 | The PHASE engine's waveforms (`firmware/src/eng_phase.c`) |
| msfa, from [Dexed](https://github.com/asb2m10/dexed) | Google Inc., Pascal Gauthier; ported to integer C for Felucca by Leo Kuroshita | Apache-2.0 | The FM6 engine's synthesis (`firmware/src/fm6_core.c`, `LICENSES/Apache-2.0-msfa.txt`) |
| DaisySP | Electrosmith, Emilie Gillet | MIT | The PHYS engine's models (`firmware/src/phys_dsp.c`; ported to fixed point by Leo Kuroshita) |
| Rings' sympathetic strings | Emilie Gillet | MIT | The PHYS engine's sympathetic strings (`firmware/src/phys_symp.c`) |
| [klattsch](https://github.com/tgies/klattsch) | Tony Gies | MIT | Design reference for the VOICE engine; no code copied. Formant data from Klatt (1980) and Hillenbrand et al. (1995) |
| JieLi [AC79 SDK](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK) | JieLi | Apache-2.0 | Three files read from your SDK checkout at build time and placed in the package; none of them is in this repository |

## The SMK-37 port

- **M. Geisler (genkigenki / makkuro)**, [fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37),
  [Snoop37](https://github.com/genkigenki/Snoop37) and [genkigenki.github.io/smk37](https://genkigenki.github.io/smk37/)
  (GPL-3.0-only in this tree): the SMK-37 board layer (keys and velocity, pads and their LEDs, the screen, faders,
  wheels, encoders, the flash map and its guards, the update path), refusing another board's package before anything
  is erased, the pad sound picker and the FADER BANK screen, the TRS MIDI OUT driver, the installer's update library
  (`web/smkota.js`) and the reverse engineering and documentation of the SMK-37. Through Clipper Pro.
- **ChanceTheMaker**, [sloop-smk](https://github.com/ChanceTheMaker/sloop-smk): ideas Clipper Pro rebuilt (the sustain
  pedal's measured levels, a flush so no note hangs on the TRS output, the installer's MIDI session in a disposable
  document), and the study of Windows USB installs.

Clipper Pro's own credits page is in its repository:
[isod89/clipper-pro CREDITS.md](https://github.com/isod89/clipper-pro/blob/main/CREDITS.md).

## Data: samples and fonts

| What | Authors | Licence | Where |
| --- | --- | --- | --- |
| Instrument and drum samples: VSCO-2 CE and VCSL | Versilian Studios | CC0 1.0 | `assets/samples-cc0/`, `ATTRIBUTION.txt` there |
| SCRCH | Sonic Pi | CC0 1.0 | `assets/samples-cc0/` |
| The hip-hop pack's E.PIANO: [GregSullivan.E-Pianos](https://github.com/sfzinstruments/GregSullivan.E-Pianos) | Greg Sullivan (<http://www.sullivang.net/>), mapped by kinwie | CC BY 3.0, credit: Greg Sullivan | `assets/hiphop-pack/`, `CREDITS.txt` there |
| Terminus font 8x16: the screen and the web editor's font | Dimitar Toshkov Zhekov | SIL OFL 1.1 | `assets/fonts/ter-u16n.bdf`, `assets/fonts/Terminus-LICENSE.txt` |
| Fukiai icon font, web editor only | Hügelton Instruments | MIT | `web/fukiai.ttf`, `web/FUKIAI-LICENSE.txt` |

The icon atlas, the logo, the boot splash, the web app icons, the factory wavetables, the
synthesised drum kits and Clipper's synthesised sample sources are drawn or made by code in this tree (GPL-3.0-only);
nothing is traced.

## Ideas, research and tools

- **AL-255**, [FM-1-RE](https://github.com/AL-255/FM-1-RE): the FM-1's reverse engineering.
- **kagaimiq**, [jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool) and the JieLi research: the full flash backup.
- **renebohne**, **@ordenador** (Mario Faúndez Vidal) and the SLOOP and Felucca communities: through SLOOP.
- **Sven (The Firmware Hub)**: the heads-up on the FM-1's newer flash chip (Clipper 1.3.1).
- **LEVER and RIBBON** follow the behaviour of FuMi-1's rocker, CS-1's ribbon and Penumbra (a held key gating the bend);
  Stallion's own code, nothing taken from them.
- The supersaw follows Adam Szabo's analysis, *How to Emulate the Super Saw* (2010). `.scl` is Manuel Op de Coul's
  Scala format. VPS follows Kleimola et al. (DAFx 2011).

If your work is in Stallion and missing here, or credited wrongly, please open an issue: it will be fixed.

M-VAVE, FM-1 and SMK-37 are trademarks of their respective owners. Stallion is independent firmware and is not
affiliated with or endorsed by them.
