// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated by tools/bigint-tester.py.

var data = [{
  a: -0xe813d76adc0a177778c0c232c595e8572b783210f4a7009d7c1787n,
  b: 0x9en,
  r: -0x3a04f5dab70285ddde30308cb1657a15cade0c843d29c0275f05e1c000000000000000000000000000000000000000n
}, {
  a: -0xded00dff14554cn,
  b: 0xd9n,
  r: -0x1bda01bfe28aa98000000000000000000000000000000000000000000000000000000n
}, {
  a: -0x50c2df089209be37998c8f6e30c4c95a61e77a9241n,
  b: 0x46n,
  r: -0x1430b7c224826f8de66323db8c3132569879dea490400000000000000000n
}, {
  a: 0x668086293eab52b52b879f00375d0f513f1ff9a93df788e38527en,
  b: -0x3n,
  r: 0xcd010c527d56a56a570f3e006eba1ea27e3ff3527bef11c70a4fn
}, {
  a: 0n,
  b: -0x1n,
  r: 0n
}, {
  a: 0x26bd28fb3e06bb0ddan,
  b: -0xcn,
  r: 0x26bd28fb3e06bb0n
}, {
  a: -0xa04dca5c74bdb1dbf2f0n,
  b: -0x3fn,
  r: -0x1409cn
}, {
  a: -0xd6b46ec3ead311e2e45ca4ae8aa9cf5acf4c2d6c61de06e9e36n,
  b: -0xan,
  r: -0x35ad1bb0fab4c478b917292ba2aa73d6b3d30b5b187781ba8n
}, {
  a: -0x1f1922aen,
  b: -0xbfn,
  r: -0x1n
}, {
  a: 0x47n,
  b: -0x2n,
  r: 0x11n
}, {
  a: -0xf64n,
  b: 0x7en,
  r: -0x3d900000000000000000000000000000000n
}, {
  a: -0x78dn,
  b: -0x85en,
  r: -0x1n
}, {
  a: -0xa03bc462b00e4113a2472eb73dn,
  b: 0n,
  r: -0xa03bc462b00e4113a2472eb73dn
}, {
  a: 0x3816aa0f01396f4abe0ee7164e211ce60ce590b615d86f6e6363c0bdd513d8a0n,
  b: 0x4n,
  r: 0x3816aa0f01396f4abe0ee7164e211ce60ce590b615d86f6e6363c0bdd513d8a00n
}, {
  a: -0xaf8711d29fdecd42a255f14aafe13b9404cced5df0af8b86080af7682cben,
  b: 0x1n,
  r: -0x15f0e23a53fbd9a8544abe2955fc277280999dabbe15f170c1015eed0597cn
}, {
  a: 0x218b11dfn,
  b: 0n,
  r: 0x218b11dfn
}, {
  a: 0x263fcb4707e6fd4a21b1f646ddd90a5e78d781c272621dcaf861d6250d7be675e067n,
  b: -0x868n,
  r: 0n
}, {
  a: 0x8312b459b585847682en,
  b: 0x4en,
  r: 0x20c4ad166d61611da0b80000000000000000000n
}, {
  a: 0xb45d42e0595a4b104b0b9a177ffdc52b401b61659fe163e720621aef8c8e6191ba7an,
  b: 0x3n,
  r: 0x5a2ea1702cad25882585cd0bbffee295a00db0b2cff0b1f390310d77c64730c8dd3d0n
}, {
  a: -0x42d2c4n,
  b: 0xfdn,
  r: -0x85a588000000000000000000000000000000000000000000000000000000000000000n
}];

var error_count = 0;
for (var i = 0; i < data.length; i++) {
  var d = data[i];
  var r = d.a << d.b;
  if (d.r !== r) {
    print("Input A:  " + d.a.toString(16));
    print("Input B:  " + d.b.toString(16));
    print("Result:   " + r.toString(16));
    print("Expected: " + d.r);
    print("Op: <<");
    error_count++;
  }
}
if (error_count !== 0) {
  print("Finished with " + error_count + " errors.")
  quit(1);
}
