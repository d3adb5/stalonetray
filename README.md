[![Build Status](https://api.cirrus-ci.com/github/kolbusa/stalonetray.svg)](https://cirrus-ci.com/github/kolbusa/stalonetray)

# STAnd aLONE TRAY

## Description

Stalonetray is a STAnd-aLONE system TRAY (notification area).x  It has minimal build and run-time dependencies: the Xlib
only. Stalonetray runs under virtually any window manager.

To start using stalonetray, just copy `stalonetrayrc.sample` to `~/.stalonetrayrc` or to
`$XDG_CONFIG_HOME/stalonetrayrc`. It is well-commented and should suffice for a quick start.

## Installation

Package managers are the most convenient way to install stalonetray. It is packaged for several Linux distributions and
BSD variants. On Debian and Ubuntu, run:
```sh
sudo apt install stalonetray
```

On Fedora run:
```sh
sudo dnf install stalonetray
```

## Building from source

Stalonetray uses autotools. It requires Xlib development packages to be installed. Building documentation requires
dockbook and `xsltproc`.

On Debian and Ubuntu, run:
```sh
sudo apt install autoconf automake docbook-xsl libxpm-dev libx11-dev xsltproc libxinerama-dev
```

After that run:

```sh
aclocal && autoheader && autoconf && automake --add-missing
./configure
```

The following message should be displayed after a successful configuration:
```
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

