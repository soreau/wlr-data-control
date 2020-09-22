# wlr-data-control
Simple wlroots clipboard monitoring example

Requires a compositor that implements **wlr-data-control-unstable-v1** protocol.

The program offers a selection string and monitors for offers. After running the program, pasting the main (ctrl+v) or primary (middle click) selection should reveal the string. New main or primary selections are output to stdout. Only mime type "text/plain;charset=utf-8" is supported.
