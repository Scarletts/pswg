pswg(1): pony static website generator
======================================

This project was inspired by my disappointment with other static site
generators. It's supposed to be small, POSIX compliant, but still
useful enough to not be too annoying.

"Fancy" features include the ability to generate a news archive
and Atom feed. For more information, you might want to read the
man page (don't forget to read it!)

It probably still needs some testing, particularly on systems
that use special (not UTC+0) timezones.

System requirements
-------------------

A relatively modern POSIX-like environment, with a compiler like
gcc or clang, and a mdoc reader.

Installation
------------
    $ make
    # make install
    $ make clean

