These are unofficial targets for [INAV](https://github.com/iNavFlight/inav).
Some of the targets provide support for flight controller boards that do not have official INAV support.
Others add features to boards that are supported, such as adding PINIO or extra servo outputs.

Targets can be found in [src/main/target/](src/main/target/)

Targets available here include:

GEPRCF722
GRAVITYF7
IFLIGHTF7_SXEMINI
MATEKF405SE_PINIO
MATEKF405SE_PINIO2
OMNIBUSF4_PINIO:
OMNIBUSF4_PINIOPRO
OMNIBUSF4_PINIOV3
OMNIBUSF4_PINIOV3_S5_S6_2SS
OMNIBUSF4_PINIOV3_S5S6_SS
OMNIBUSF4_PINIOV3_S6_SS
SKYSTARSF405hd
SKYSTARSF722HDPRO

New additions are very much welcomed. If you've made your own target, please
send a pull request or send me a zip of your files and I'll add it here.
Improvements are also very much welcomed. ANy addtions or improvements you
make, please send a PR or let me know.

Here is some information about how you can make your own target, either to support
an FC that isn't already supported, or to remap resources:
[Building Custom Firmware](https://github.com/iNavFlight/inav/wiki/Building-custom-firmware)

Geting a new target added to the *offical* inav distribution has certain
[requirements](https://github.com/iNavFlight/inav/blob/master/docs/policies/NEW_HARDWARE_POLICY.md).
On the other hand, all targets are welcome here.

Please add in your target folder a readme.txt file saying what your target is for and how you've tested it.

Note we cannot generally provide support for the code here. This is just a 
collection of targets that have been contributed by people like you. The maintainer
of this repository (Sensei) didn't write this code and may not own the hardware.
The only exception is Airbot Omnibus boards. The maintainer of this repo
does own Omnibus flight controllers and is familiar with ONLY those boards.

These files are not a product of the official inav project and are not endorsed 
by the project or affiliated with it. These files may or may not work well for you.
They have been provided by people who made them for their own use and report that the files work for them.


