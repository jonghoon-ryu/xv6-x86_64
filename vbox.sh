#!/bin/sh
# boot esp.img in a VirtualBox VM with UEFI firmware.
# the VM's first serial port (COM1, the xv6 console) is the unix
# socket vbox/serial.sock; connect with: socat - UNIX-CONNECT:vbox/serial.sock
set -e
VM=xv6-x86_64
DIR=$(pwd)/vbox
CPUS=${CPUS:-2}
mkdir -p "$DIR"

# stop and forget any earlier run.
if VBoxManage showvminfo $VM >/dev/null 2>&1; then
  VBoxManage controlvm $VM poweroff >/dev/null 2>&1 || true
  sleep 2
  VBoxManage unregistervm $VM --delete >/dev/null 2>&1 || true
fi
VBoxManage closemedium disk "$DIR/esp.vdi" --delete >/dev/null 2>&1 || true
rm -f "$DIR/esp.vdi" "$DIR/serial.sock"

VBoxManage convertfromraw esp.img "$DIR/esp.vdi" --format VDI >/dev/null
VBoxManage createvm --name $VM --ostype Other_64 --basefolder "$DIR" --register >/dev/null
VBoxManage modifyvm $VM --firmware efi --memory 512 --cpus $CPUS --ioapic on \
  --uart1 0x3F8 4 --uartmode1 server "$DIR/serial.sock"
VBoxManage storagectl $VM --name SATA --add sata --controller IntelAhci >/dev/null
VBoxManage storageattach $VM --storagectl SATA --port 0 --device 0 \
  --type hdd --medium "$DIR/esp.vdi"
VBoxManage startvm $VM --type headless
