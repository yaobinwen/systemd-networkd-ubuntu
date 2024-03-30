# systemd-networkd

## Overview

This repository contains all the necessary source files to build the executable `systemd-networkd` on Ubuntu.

I created this repository only for learning purpose. The entire `systemd` codebase is sort of large and I could get lost easily when I was only interested in the source code of `systemd-networkd`. So I spent time and moved the `systemd-networkd`-related files here and made them compile. Therefore, the code in this repository belongs to the original `systemd` authors and is under the same license as the original `systemd` code. See their [systemd/README](https://github.com/systemd/systemd/blob/main/README) for the license.

## How to build

- 1). `cd` in the root directory of this repository.
- 2). `./install-deps.sh` to install the build dependencies.
  - Note I only tested this script on my machine so it may not install all the needed dependencies on your machine.
- 3). `./configure` to generate the `build` folder.
  - a). If you want to add more C flags, define the environment variable `CFLAGS`/`CXXFLAGS` before running `configure`. For example, `export CFLAGS=-rdynamic`. See [`configure`](./configure) for more details.
- 4). `make` to build the code.
  - a). If you want to display the actual `gcc` command line for debugging, define the environment variable `NINJA_VERBOSE` before running `make`. For example, `export NINJA_VERBOSE=1`. See [`Makefile`](./Makefile) for more details.
- 5). When the build is done, the executable `systemd-networkd` can be found under the `build` folder.

## Debug environment setup & build

To make debugging easier, I added some supportive files and scripts:
- `debug-env` contains the files that set up a debugging environment to play with the locally built `systemd-networkd`.
- `debug-env.sh` is the script that installs/removes the debugging environment.
- `debug-build.sh` is the script that, after the debugging environment is set up, build the code and restart `systemd-networkd.service`.

## Enable debug logging

Set the environment variable `SYSTEMD_LOG_LEVEL=debug` before running `systemd-networkd`.

If you replace the real `systemd-networkd` with the version you built by yourself, enabling debugging logs can also be done by dropping the following `systemd-networkd` configuration file as `/etc/systemd/system/systemd-networkd.service.d/10-debug.conf`, followed by `systemctl restart systemd-networkd.service`:

```ini
[Service]
Environment=SYSTEMD_LOG_LEVEL=debug
```

## Entry point

The entry point (i.e., the `main` function) is in `src/network/networkd.c`.
