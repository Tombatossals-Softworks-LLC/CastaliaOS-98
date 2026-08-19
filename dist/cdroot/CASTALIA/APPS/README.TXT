CastaliaOS 98 PE - Add-on packages (.CAPP)
==========================================

Drop .CAPP packages in this folder. At startup the shell scans this directory,
validates each package (magic, version, bounds, and a CRC-32 integrity check),
and logs the manifest of every valid one. Corrupt or unrecognized files are
reported and ignored -- discovery never executes package code.

A .CAPP file is a small, self-describing container: a manifest (name, version,
author, description) plus typed sections (code, resource, icon, help). The
format and the plugin ABI a code section targets are documented in
include/castalia/capp.h and include/castalia/capp_abi.h.

Authoring a package (host tool, built by the top-level Makefile):

    mkcapp -o MYAPP.CAPP --name "My App" --version 1.0.0 \
           --author "You" --desc "What it does" \
           [--icon icon.bin] [--help help.txt] [--code plugin.bin]

Loading and running a package's code is a later milestone; today the format is
defined, packages can be built and shipped, and the shell finds and inspects
them safely.
