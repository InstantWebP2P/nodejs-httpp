# node-httpp

![C/C++ CI](https://github.com/InstantWebP2P/node-httpp/workflows/C/C++%20CI/badge.svg)

HTTPP - Run HTTP over UDP and Bring Web in Peer or P2P styles.

* Consists of four modules: udt.js, httpp.js, udts.js, httpps.js with the compatible api to net.js,http.js,tls.js and https.js

[Discussion group](https://groups.google.com/d/forum/iwebpp)

[Wiki page](https://github.com/InstantWebP2P/node-httpp/wiki/An-introduction-to-node-httpp)

### Features

* Run HTTP and HTTPS over UDP, taking UDP high performance
* Support multi-process/thread UDT server by utilizing multi-cores' CPU
* Enhance UDT security by authenticating both control and data packet
* Configurable Congestion Control
* Per-socket network performance monitor
* Per-socket bandwidth limitation
* Extend client/central server style web service transparently
* Easy to use API, reuse existing http/web and node.js technology
* Support windows/linux/OSx, RaspberryPi, Android

### TODO

* Support iOS
* Add more test cases
* Improve documents
* RFC protocol draft


### third-party source

* [UDT4](http://udt.sourceforge.net)


### Support us

* Welcome contributing on document, codes, tests and issues


Evented I/O for V8 javascript.
===

To checkout the sourcecode:

    git clone https://github.com/InstantWebP2P/node-httpp.git


### Build on Linux with Docker

  building:
  
    ./appbld ./configure --prefix=/appins
    ./appbld make -j 6
    ./appbld make install
    
  packaging and start Node.js REPL:
    
    ./apppkg
    
### To build on other platforms

Prerequisites (Unix only):

    * Python 2.6 or 2.7
    * GNU Make 3.81 or newer
    * libexecinfo (FreeBSD and OpenBSD only)

Unix/Macintosh:

    ./configure
    make
    make install

Cross compile for Raspberry Pi:

    setup toolchain env first, then
    source cross-compile-pi.sh

Cross compile for Android:

    setup Android NDK env first, then
    source android-configure NDK_PATH
    make -C out  

If your python binary is in a non-standard location or has a
non-standard name, run the following instead:

    export PYTHON=/path/to/python
    $PYTHON ./configure
    make
    make install

Windows:

    vcbuild.bat
    

### Stay on NPM v2.15.9

    * npm i npm@2.15.9 -g
    

### To run the tests:

Unix/Macintosh:

    make test

Windows:

    vcbuild.bat test

### To build the documentation:

    make doc

### To read the documentation:

    man doc/node.1

Resources for Newcomers
---
  - [The Wiki](https://github.com/joyent/node/wiki)
  - [nodejs.org](http://nodejs.org/)
  - [how to install node.js and npm (node package manager)](http://joyeur.com/2010/12/10/installing-node-and-npm/)
  - [list of modules](https://github.com/joyent/node/wiki/modules)
  - [searching the npm registry](http://search.npmjs.org/)
  - [list of companies and projects using node](https://github.com/joyent/node/wiki/Projects,-Applications,-and-Companies-Using-Node)
  - [node.js mailing list](http://groups.google.com/group/nodejs)
  - irc chatroom, [#node.js on freenode.net](http://webchat.freenode.net?channels=node.js&uio=d4)
  - [community](https://github.com/joyent/node/wiki/Community)
  - [contributing](https://github.com/joyent/node/wiki/Contributing)
  - [big list of all the helpful wiki pages](https://github.com/joyent/node/wiki/_pages)
  - [The SO_REUSEPORT socket option](https://lwn.net/Articles/542629/)
