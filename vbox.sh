#!/bin/sh
# [platform: VirtualBox] boot usb.img in a VirtualBox VM with UEFI firmware.
# in a window (the default), type into the window; the serial port
# (COM1) output also goes to vbox/serial.log.
# headless (VBOXTYPE=headless), the serial port is the unix socket
# vbox/serial.sock; connect with: socat - UNIX-CONNECT:vbox/serial.sock
# (a socket that nobody is connected to makes the serial port stall,
# which is why the window uses a log file instead.)
set -e
VM=xv6-x86_64
DIR=$(pwd)/vbox
CPUS=${CPUS:-2}
TYPE=${VBOXTYPE:-gui} # gui: a window with the screen console; headless: no window
RES=${VBOXRES:-2560x1440} # screen size that the UEFI firmware sets up
SCALE=${VBOXSCALE:-1} # window zoom; 1 draws each pixel exactly, the sharpest
mkdir -p "$DIR"

# stop and forget any earlier run.
if VBoxManage showvminfo $VM >/dev/null 2>&1; then
  VBoxManage controlvm $VM poweroff >/dev/null 2>&1 || true
  sleep 2
  VBoxManage unregistervm $VM --delete >/dev/null 2>&1 || true
fi
VBoxManage closemedium disk "$DIR/esp.vdi" --delete >/dev/null 2>&1 || true
rm -f "$DIR/esp.vdi" "$DIR/serial.sock" "$DIR/serial.log"
if [ "$TYPE" = headless ]; then
  SERIAL="server $DIR/serial.sock"
else
  SERIAL="file $DIR/serial.log"
fi

VBoxManage convertfromraw usb.img "$DIR/esp.vdi" --format VDI >/dev/null
VBoxManage createvm --name $VM --ostype Other_64 --basefolder "$DIR" --register >/dev/null
VBoxManage modifyvm $VM --firmware efi --memory 512 --cpus $CPUS --ioapic on --vram 32 \
  --uart1 0x3F8 4 --uartmode1 $SERIAL
VBoxManage setextradata $VM GUI/ScaleFactor $SCALE
VBoxManage setextradata $VM VBoxInternal2/EfiGraphicsResolution $RES
VBoxManage storagectl $VM --name SATA --add sata --controller IntelAhci >/dev/null
VBoxManage storageattach $VM --storagectl SATA --port 0 --device 0 \
  --type hdd --medium "$DIR/esp.vdi"
VBoxManage startvm $VM --type $TYPE
