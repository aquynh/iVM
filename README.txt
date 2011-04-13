how to compile:

./configure --target-list=arm-softmmu --enable-sdl
make

How to run:

./arm-softmmu/qemu-system-arm -M iphone2g -option-rom iBoot-1.0.2.m68ap.RELEASE -option-rom iphone1-bootrom.bin -pflash nordump.bin -serial stdio -skin ./skin/devices/iphone2g/skin.xml

How do i contribute:

Message me on twitter @cmwdotme and submit patches.

Notes:

This was done to help with the discovery and exploration of new exploits on
the iPhone. The test was to see if we could emulate enough to get iBoot
running then to try and get the kernel booting. The last step would be 
trying to do a full restore via itunes.


Credit:

Thanks to Dre and iDroid guys specifically (Bluerise, ricky26, CPICH) for their help
and of course thanks to comex/chpwn for their support and *motivation*

p.s
Chronicdev rocks


