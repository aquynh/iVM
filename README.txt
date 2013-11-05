iVM
WINE-Like subsystem + Open Source VM Stack + Android AOSP

how to compile:

./configure --target-list=arm-softmmu --enable-sdl --enable-skinning
make

How to run:

./arm-softmmu/qemu-system-arm -M ipad1g -option-rom iBoot.k48ap.RELEASE.unencrypted -global s5l8930_h2fmi0.file="0,ce0.bin;2,ce2.bin" -global s5l8930_h2fmi1.file="0,ce1.bin;2,ce3.bin" -pflash ipadnor.bin -gdb tcp::6666 -nographic -S -serial file:serial.txt -monitor stdio -smp 2


Credit:

Thanks to Dre and iDroid guys specifically (Bluerise, ricky26, CPICH) for their help
and of course thanks to comex/chpwn for their support and *motivation*



