# Photon Chess Client

HTTP chess client for interfacing with chess REST server: https://github.com/Austinpayne/node-chess-js-server

## Dependencies

* HttpClient: https://github.com/Austinpayne/HttpClient (based on https://github.com/nmattisson/HttpClient, however, heavily modified to fix bugs and be more C compliant)
* frozen: https://github.com/cesanta/frozen (json scanf decoding/encoding)
* chess-serial-protocol: https://github.com/Austinpayne/chess-serial-protocol

## Compiling

Because the photon chess client implements its own HTTP handling, the Spark system mode has been set to SEMI_AUTOMATIC (see: https://docs.particle.io/reference/firmware/core/#system-modes) and does not use the Particle Cloud. Therefore, the photon firmware should be manually updated after compiling. I do the following:

1. `git clone` this repo and the dependencies above
2. Turn the dependencies into private libraries (see: https://docs.particle.io/guide/tools-and-features/cli/core/#contributing-libraries)
3. Compile using Particle Desktop IDE (see: https://docs.particle.io/guide/tools-and-features/dev/#getting-started)
4. Install Particle CLI (see: https://docs.particle.io/guide/tools-and-features/cli/core/)
5. Put Photon in DFU mode (see: https://docs.particle.io/guide/getting-started/modes/photon/#dfu-mode-device-firmware-upgrade-)
6. Upgrade firmware using `particle flash --usb <.bin-file-compiled-in-step-3>`

## Direct Control

By default, the chess client will try and join the first available game. To access direct control over the photon (and forward commands via seral to the stm32) connect to the photon serial, restart the photon, and press any key within the first 5 seconds of boot.
