# STAnd aLONE TRAY [![Build][badge-build]][yaml-build] [![Lint][badge-lint]][yaml-lint]

[badge-build]: https://github.com/d3adb5/stalonetray/actions/workflows/build.yml/badge.svg
[yaml-build]: https://github.com/d3adb5/stalonetray/actions/workflows/build.yml
[badge-lint]: https://github.com/d3adb5/stalonetray/actions/workflows/lint.yml/badge.svg
[yaml-lint]: https://github.com/d3adb5/stalonetray/actions/workflows/lint.yml

## Maintenance status

This project was originally developed by [Roman Dubtsov (kolbusa)][gh-kolbusa]
and recently changed hands. Roman is still involved with, but no longer
actively maintains the project.

To him goes all the credit for creating and maintaining this project for many
years.

[gh-kolbusa]: https://github.com/kolbusa

## Description

Stalonetray is a STAnd-aLONE system TRAY (notification area).  It has minimal
build and run-time dependencies: the Xlib only. Stalonetray runs under
virtually any window manager.

To start using stalonetray, just copy `stalonetrayrc.sample` to
`~/.stalonetrayrc` or to `$XDG_CONFIG_HOME/stalonetrayrc`. It is well-commented
and should suffice for a quick start.

## Installation

Package managers are the most convenient way to install stalonetray. It is
packaged for several Linux distributions and BSD variants. On Debian and
Ubuntu, run:

```sh
sudo apt install stalonetray
```

On Fedora run:

```sh
sudo dnf install stalonetray
```

## Building from source

Stalonetray uses autotools. It requires Xlib development packages to be
installed. Building documentation requires docbook and `xsltproc`.
Multi-monitor support requires `libxinerama`.

On Debian and Ubuntu, run:

```sh
sudo apt install \
  autoconf automake docbook-xsl libxpm-dev libx11-dev xsltproc libxinerama-dev
```

After that run:

```sh
aclocal && autoheader && autoconf && automake --add-missing
./configure
```

The following message should be displayed after a successful configuration:

```text
*
* Debug                  : yes
* Native KDE support     : yes
* XPM background support : yes
* Multi-monitor support  : yes
* Graceful exit support  : yes
* Build documentation    : yes
*
```

Then run:

```sh
make
make install
```
