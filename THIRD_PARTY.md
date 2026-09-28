# Third-party code and provenance

- [jcrona/tamalib](https://github.com/jcrona/tamalib), commit `ce304d55f9a73c60232ce3f552e7983db3fa399c`, copied without edits into `deps/tamalib/`. Source headers grant GPL version 2 or later; the unmodified `deps/tamalib/LICENSE` carries the GPLv2 text. `deps/hal_types.h` and `platform/tama/` are this port's integration code.
- [PicoPlus-devel/pico_shared](https://github.com/PicoPlus-devel/pico_shared), commit `df683bf00ff95e1ceda88f132bbf72f8cc7cadb0`, a minimal source subset in `deps/pico_shared/`, GPLv3 (`deps/pico_shared/LICENSE`). Its `pico_fatfs` component retains its own BSD-style license in `deps/pico_shared/drivers/pico_fatfs/LICENSE`; FatFs notices remain in its source.
- Raspberry Pi Pico SDK and Pico-PIO-USB are build-time dependencies supplied by the builder. They are not copied into this repository; follow their own upstream notices and licenses.

No commercial Tamagotchi ROM or saved state is included. The public-board integration is a new build and is not covered by the earlier development-board hardware result.
