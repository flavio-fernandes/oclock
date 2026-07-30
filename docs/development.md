# Development sandbox

The production clock ran on a Raspberry Pi Zero with Raspbian 8 (Jessie).
The practical modern equivalent for development on the Intel Mac mini is a
Debian 12 (Bookworm) Incus VM:

```sh
incus launch images:debian/12/cloud oclock-dev --vm \
  --device root,size=12GiB \
  --config limits.cpu=2 \
  --config limits.memory=2GiB

incus exec oclock-dev -- cloud-init status --wait
incus exec oclock-dev -- bash -lc \
  'apt-get update && apt-get install -y build-essential git libevent-dev libmosquitto-dev libmosquittopp1 valgrind gdb curl ca-certificates pkg-config'
```

Copy a local checkout into the VM and build it:

```sh
incus file push --recursive oclock.git oclock-dev/root/
incus exec oclock-dev -- chown -R root:root /root/oclock.git
incus exec oclock-dev -- bash -lc 'cd /root/oclock.git && make sandbox && make test'
```

The sandbox binary uses `src/fakeWiringPi.cpp`; it exercises the application,
threading, HTTP, MQTT client, rendering logic, and clean shutdown without
touching GPIO. The VM is x86-64, while the original Pi Zero is ARMv6 and its
GPIO devices and bit-bang timing are not virtualized. A final hardware build
and electrical/timing test must therefore still run on a Raspberry Pi:

```sh
make hardware
sudo ./oclock -b 0.0.0.0 -p 8080
```

The HTTP server defaults to loopback port 8080. Binding to `0.0.0.0` exposes
an unauthenticated device-control API and should only be done on a trusted
network or behind an authenticated reverse proxy.
