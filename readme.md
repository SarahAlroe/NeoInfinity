# NeoInfinity animated neopixel infinity symbol

KiCad board files and Arduino code for building a little animated infinity symbol. Makes use of [
Spence Kondes megaTinyCore](https://github.com/SpenceKonde/megaTinyCore) Arduino core and libraries for driving 27 NeoPixels with an ATtiny412. Can run from a 5V source or battery power. Implements temporal error diffusion to obtain lower brightness levels while maintaining reasonable color detail.

## Assembly

A stencil is not necessary but recommended for the top layer.

Components:
- 17 x 100nF 0603 capacitors. Most if not all can probably be skipped without issue.
- 1 x 10µF 0603 capacitor.
- 27 x WS2812B 5x5mm Addressable RGB LEDs.
- 1 x ATtiny412 SOIC package.
- 1 x EVQP2 push button.
- 1 x 3 pin SMD 2.54mm socket
- 1 x JST PH SMD connector (JST_S2B-PH-SM4-TB)

## Software

Only dependent on the [megaTinyCore](https://github.com/SpenceKonde/megaTinyCore/blob/master/Installation.md) and its libraries.  
Some notes:
- Try to stick with a 20MHz clock speed or as high as voltage allows for nicer persistence of vision. Animations speed is kept consistent across clock speeds.
- Disable milis() and use minimal printf() option to make code fit. Current compile lands at ~95% flash.


## Extras

- PatternPositions - Calculations of where each neopixel is along various directions. 
- StencilCase - SVG and FreeCad model for holding PCBs and a 120x120mm stencil positioned.  
- Without QR block - Version of the PCB layout without the 8x8mm square on the back replaced with a unique qr code on production.