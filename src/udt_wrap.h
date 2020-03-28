// Copyright tom zhou<iwebpp@gmail.com>, 2012.
//
// udt_wrap.h, ported from tcp_wrap.h with udt transport binding
//

// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

#ifndef UDT_WRAP_H_
#define UDT_WRAP_H_
#include "stream_wrap.h"

namespace node {

class UDTWrap : public StreamWrap {
 public:
  static v8::Local<v8::Object> Instantiate();
  static UDTWrap* Unwrap(v8::Local<v8::Object> obj);
  static void Initialize(v8::Handle<v8::Object> target);

  uv_udt_t* UVHandle();

 private:
  UDTWrap(v8::Handle<v8::Object> object);
  ~UDTWrap();

  static v8::Handle<v8::Value> New(const v8::Arguments& args);
  static v8::Handle<v8::Value> GetSockName(const v8::Arguments& args);
  static v8::Handle<v8::Value> GetPeerName(const v8::Arguments& args);
  static v8::Handle<v8::Value> SetNoDelay(const v8::Arguments& args);
  static v8::Handle<v8::Value> SetKeepAlive(const v8::Arguments& args);
  static v8::Handle<v8::Value> Bind(const v8::Arguments& args);
  static v8::Handle<v8::Value> Bind6(const v8::Arguments& args);
  static v8::Handle<v8::Value> Listen(const v8::Arguments& args);
  static v8::Handle<v8::Value> Connect(const v8::Arguments& args);
  static v8::Handle<v8::Value> Connect6(const v8::Arguments& args);
  static v8::Handle<v8::Value> Open(const v8::Arguments& args);

  // set socket in rendezvous mode for p2p connection
  static v8::Handle<v8::Value> SetSocketRendez(const v8::Arguments& args);

  // set socket qos
  static v8::Handle<v8::Value> SetSocketQos(const v8::Arguments& args);

  // set socket maxim bandwidth
  static v8::Handle<v8::Value> SetSocketMbw(const v8::Arguments& args);

  // set socket maxim buffer size
  static v8::Handle<v8::Value> SetSocketMbs(const v8::Arguments& args);

  // set socket security mode
  static v8::Handle<v8::Value> SetSocketSec(const v8::Arguments& args);

  // set socket if REUSE ADDRESS
  static v8::Handle<v8::Value> SetReuseAddr(const v8::Arguments &args);

  // set socket if support REUSE ADDRESS
  static v8::Handle<v8::Value> SetReuseAble(const v8::Arguments &args);

  // bind socket in existing udp/fd
  static v8::Handle<v8::Value> Bindfd(const v8::Arguments& args);

  // get udp/fd associated with UDT socket
  static v8::Handle<v8::Value> Getudpfd(const v8::Arguments& args);

  // punch hole for p2p/stun session
  static v8::Handle<v8::Value> Punchhole(const v8::Arguments& args);
  static v8::Handle<v8::Value> Punchhole6(const v8::Arguments& args);

  // networking performance monitor
  static v8::Handle<v8::Value> GetNetPerf(const v8::Arguments& args);

  static void OnConnection(uv_stream_t* handle, int status);
  static void AfterConnect(uv_connect_t* req, int status);

  uv_udt_t handle_;
};


}  // namespace node


#endif  // UDT_WRAP_H_
