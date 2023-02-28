# catterm - a minimalist serial port terminal program

`catterm` is the minimalist "terminal emulation" program for serial
ports. It's primary target is the people who work a lot with hardware
platforms that use serial ports for debugging/console etc.

Unlike many other similar programs, `catterm` does absolute minimum. It
doesn't interpret ESC-sequences, relying on a Linux terminal, doesn't deal
phone numbers using modem AT commands and so on.

It's primary purpose is to be simple, stupid, reliable and predictable
like a piece of wood.

It doesn't use serial port modem control lines (RTS/CTS and DTR/DST), so
it feels quite happy with a 3-wire simple null-modem cable as well as
with full-wired null-modem.

It was tested with the standard PC serial port as well as with USB
serial ports, USB modems etc and works with all of them.

## Building

Building is simple:

```
$ make
```

There are no `./configure` magic, no external dependencies etc. Just a
bare `make`

## Usage

`catterm` understands the following command line options:

```
usage:
    catterm [options] line

options:
    -c       -- suppress control characters on output
    -d delay -- delay after each character sent
                delay parameter is:
                    NNN[us] - microseconds
                    NNNms   - milliseconds
                    NNN%    - percent of character transmit time
    -n arg   -- send new line as:
                    lf      - '\n'
                    cr      - '\r' (this is default)
                    crlf    - '\r' + '\n'
                    lfcr    - '\n' + '\r'
    -s speed -- line speed (default is 115200)
    -x char  -- use ctrl-char as exit char (default is ctrl-X)
    -t file  -- save ("tee") output to file
    -h       -- print this help screen
```

<!-- vim:ts=8:sw=4:et:textwidth=72
-->
