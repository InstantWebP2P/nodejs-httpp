// Copyright tom zhou<iwebpp@gmail.com>, 2012.
//
// udt_wrap.cc, ported from tcp_wrap.cc with udt transport binding
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

#include "node.h"
#include "node_buffer.h"
#include "req_wrap.h"
#include "handle_wrap.h"
#include "stream_wrap.h"
#include "udt_wrap.h"

#include <stdlib.h>

// Temporary hack: libuv should provide uv_inet_pton and uv_inet_ntop.
#if defined(__MINGW32__) || defined(_MSC_VER)
  extern "C" {
#   include <inet_net_pton.h>
#   include <inet_ntop.h>
  }
# define uv_inet_pton ares_inet_pton
# define uv_inet_ntop ares_inet_ntop

#else // __POSIX__
# include <arpa/inet.h>
# define uv_inet_pton inet_pton
# define uv_inet_ntop inet_ntop
#endif

namespace node {

using v8::Arguments;
using v8::Context;
using v8::Function;
using v8::FunctionTemplate;
using v8::Handle;
using v8::HandleScope;
using v8::Integer;
using v8::Local;
using v8::Object;
using v8::Null;
using v8::Persistent;
using v8::String;
using v8::TryCatch;
using v8::Undefined;
using v8::Value;
using v8::Number;

static Persistent<Function> udtConstructor;
static Persistent<String> oncomplete_sym;
static Persistent<String> onconnection_sym;


typedef class ReqWrap<uv_connect_t> ConnectWrap;

#ifdef _WIN32
static Local<Object> AddressToJS(const sockaddr* addr, const SOCKET fd, const int udtfd);
#else
static Local<Object> AddressToJS(const sockaddr* addr, const int fd, const int udtfd);
#endif

Local<Object> UDTWrap::Instantiate() {
  // If this assert fire then process.binding('udt_wrap') hasn't been
  // called yet.
  assert(udtConstructor.IsEmpty() == false);

  HandleScope scope;
  Local<Object> obj = udtConstructor->NewInstance();

  return scope.Close(obj);
}


void UDTWrap::Initialize(Handle<Object> target) {
  HandleWrap::Initialize(target);
  StreamWrap::Initialize(target);

  HandleScope scope;

  Local<FunctionTemplate> t = FunctionTemplate::New(New);
  t->SetClassName(String::NewSymbol("UDT"));

  t->InstanceTemplate()->SetInternalFieldCount(1);

  NODE_SET_PROTOTYPE_METHOD(t, "close", HandleWrap::Close);

  NODE_SET_PROTOTYPE_METHOD(t, "readStart", StreamWrap::ReadStart);
  NODE_SET_PROTOTYPE_METHOD(t, "readStop", StreamWrap::ReadStop);
  NODE_SET_PROTOTYPE_METHOD(t, "shutdown", StreamWrap::Shutdown);

  NODE_SET_PROTOTYPE_METHOD(t, "writeBuffer", StreamWrap::WriteBuffer);
  NODE_SET_PROTOTYPE_METHOD(t, "writeAsciiString", StreamWrap::WriteAsciiString);
  NODE_SET_PROTOTYPE_METHOD(t, "writeUtf8String", StreamWrap::WriteUtf8String);
  NODE_SET_PROTOTYPE_METHOD(t, "writeUcs2String", StreamWrap::WriteUcs2String);

  NODE_SET_PROTOTYPE_METHOD(t, "bind", Bind);
  NODE_SET_PROTOTYPE_METHOD(t, "listen", Listen);
  NODE_SET_PROTOTYPE_METHOD(t, "connect", Connect);
  NODE_SET_PROTOTYPE_METHOD(t, "bind6", Bind6);
  NODE_SET_PROTOTYPE_METHOD(t, "bindfd", Bindfd);
  NODE_SET_PROTOTYPE_METHOD(t, "connect6", Connect6);
  NODE_SET_PROTOTYPE_METHOD(t, "getsockname", GetSockName);
  NODE_SET_PROTOTYPE_METHOD(t, "getpeername", GetPeerName);
  NODE_SET_PROTOTYPE_METHOD(t, "setNoDelay", SetNoDelay);
  NODE_SET_PROTOTYPE_METHOD(t, "setKeepAlive", SetKeepAlive);
  NODE_SET_PROTOTYPE_METHOD(t, "setSocketRendez", SetSocketRendez);
  NODE_SET_PROTOTYPE_METHOD(t, "setSocketQos", SetSocketQos);
  NODE_SET_PROTOTYPE_METHOD(t, "setSocketMbw", SetSocketMbw);
  NODE_SET_PROTOTYPE_METHOD(t, "setSocketMbs", SetSocketMbs);
  NODE_SET_PROTOTYPE_METHOD(t, "setSocketSec", SetSocketSec);
  NODE_SET_PROTOTYPE_METHOD(t, "punchhole", Punchhole);
  NODE_SET_PROTOTYPE_METHOD(t, "punchhole6", Punchhole6);
  NODE_SET_PROTOTYPE_METHOD(t, "getnetperf", GetNetPerf);
  NODE_SET_PROTOTYPE_METHOD(t, "getudpfd", Getudpfd);

  udtConstructor = Persistent<Function>::New(t->GetFunction());

  onconnection_sym = NODE_PSYMBOL("onconnection");
  oncomplete_sym = NODE_PSYMBOL("oncomplete");

  target->Set(String::NewSymbol("UDT"), udtConstructor);
}


UDTWrap* UDTWrap::Unwrap(Local<Object> obj) {
  assert(!obj.IsEmpty());
  assert(obj->InternalFieldCount() > 0);
  return static_cast<UDTWrap*>(obj->GetPointerFromInternalField(0));
}


uv_udt_t* UDTWrap::UVHandle() {
  return &handle_;
}


Handle<Value> UDTWrap::New(const Arguments& args) {
  // This constructor should not be exposed to public javascript.
  // Therefore we assert that we are not trying to call this as a
  // normal function.
  assert(args.IsConstructCall());

  HandleScope scope;
  UDTWrap* wrap = new UDTWrap(args.This());
  assert(wrap);

  return scope.Close(args.This());
}


UDTWrap::UDTWrap(Handle<Object> object)
    : StreamWrap(object, (uv_stream_t*) &handle_) {
  int r = uv_udt_init(uv_default_loop(), &handle_);
  assert(r == 0); // How do we proxy this error up to javascript?
                  // Suggestion: uv_udt_init() returns void.
  UpdateWriteQueueSize();
}


UDTWrap::~UDTWrap() {
  assert(object_.IsEmpty());
}


Handle<Value> UDTWrap::GetSockName(const Arguments& args) {
  HandleScope scope;
  struct sockaddr_storage address;

  UNWRAP(UDTWrap)

  int addrlen = sizeof(address);
  int r = uv_udt_getsockname(&wrap->handle_,
                             reinterpret_cast<sockaddr*>(&address),
                             &addrlen);

  if (r) {
    SetErrno(uv_last_error(uv_default_loop()));
    return Null();
  }

  const sockaddr* addr = reinterpret_cast<const sockaddr*>(&address);
#ifdef _WIN32
  return scope.Close(AddressToJS(addr, wrap->handle_.socket, wrap->handle_.udtfd));
#else
  return scope.Close(AddressToJS(addr, wrap->handle_.fd, wrap->handle_.udtfd));
#endif
}


Handle<Value> UDTWrap::GetPeerName(const Arguments& args) {
  HandleScope scope;
  struct sockaddr_storage address;

  UNWRAP(UDTWrap)

  int addrlen = sizeof(address);
  int r = uv_udt_getpeername(&wrap->handle_,
                             reinterpret_cast<sockaddr*>(&address),
                             &addrlen);

  if (r) {
    SetErrno(uv_last_error(uv_default_loop()));
    return Null();
  }

  const sockaddr* addr = reinterpret_cast<const sockaddr*>(&address);
#ifdef _WIN32
  return scope.Close(AddressToJS(addr, INVALID_SOCKET, -1));
#else
  return scope.Close(AddressToJS(addr, -1, -1));
#endif
}


Handle<Value> UDTWrap::GetNetPerf(const Arguments& args) {
  HandleScope scope;
  uv_netperf_t perf;

  UNWRAP(UDTWrap)

  int r = uv_udt_getperf(&wrap->handle_, &perf, 1); // clear performance data every time

  if (r) {
    SetErrno(uv_last_error(uv_default_loop()));
    return Null();
  }

  // pass performance data to JS
  Local<Object> jsobj = Object::New();

#if 0
  // global measurements
  int64_t msTimeStamp;                 // time since the UDT entity is started, in milliseconds
  int64_t pktSentTotal;                // total number of sent data packets, including retransmissions
  int64_t pktRecvTotal;                // total number of received packets
  int pktSndLossTotal;                 // total number of lost packets (sender side)
  int pktRcvLossTotal;                 // total number of lost packets (receiver side)
  int pktRetransTotal;                 // total number of retransmitted packets
  int pktSentACKTotal;                 // total number of sent ACK packets
  int pktRecvACKTotal;                 // total number of received ACK packets
  int pktSentNAKTotal;                 // total number of sent NAK packets
  int pktRecvNAKTotal;                 // total number of received NAK packets
  int64_t usSndDurationTotal;          // total time duration when UDT is sending data (idle time exclusive)
#endif
  static Persistent<String> msTimeStamp;
  static Persistent<String> pktSentTotal;
  static Persistent<String> pktRecvTotal;
  static Persistent<String> pktSndLossTotal;
  static Persistent<String> pktRcvLossTotal;
  static Persistent<String> pktRetransTotal;
  static Persistent<String> pktSentACKTotal;
  static Persistent<String> pktRecvACKTotal;
  static Persistent<String> pktSentNAKTotal;
  static Persistent<String> pktRecvNAKTotal;
  static Persistent<String> usSndDurationTotal;

  static Persistent<String> pktSent;
  static Persistent<String> pktRecv;
  static Persistent<String> pktSndLoss;
  static Persistent<String> pktRcvLoss;
  static Persistent<String> pktRetrans;
  static Persistent<String> pktSentACK;
  static Persistent<String> pktRecvACK;
  static Persistent<String> pktSentNAK;
  static Persistent<String> pktRecvNAK;
  static Persistent<String> mbpsSendRate;
  static Persistent<String> mbpsRecvRate;
  static Persistent<String> usSndDuration;

  static Persistent<String> usPktSndPeriod;
  static Persistent<String> pktFlowWindow;
  static Persistent<String> pktCongestionWindow;
  static Persistent<String> pktFlightSize;
  static Persistent<String> msRTT;
  static Persistent<String> mbpsBandwidth;
  static Persistent<String> byteAvailSndBuf;
  static Persistent<String> byteAvailRcvBuf;

  if (msTimeStamp.IsEmpty()) {
	  msTimeStamp        = NODE_PSYMBOL("msTimeStamp");
	  pktSentTotal       = NODE_PSYMBOL("pktSentTotal");
	  pktRecvTotal       = NODE_PSYMBOL("pktRecvTotal");
	  pktSndLossTotal    = NODE_PSYMBOL("pktSndLossTotal");
	  pktRcvLossTotal    = NODE_PSYMBOL("pktRcvLossTotal");
	  pktRetransTotal    = NODE_PSYMBOL("pktRetransTotal");
	  pktSentACKTotal    = NODE_PSYMBOL("pktSentACKTotal");
	  pktRecvACKTotal    = NODE_PSYMBOL("pktRecvACKTotal");
	  pktSentNAKTotal    = NODE_PSYMBOL("pktSentNAKTotal");
	  pktRecvNAKTotal    = NODE_PSYMBOL("pktRecvNAKTotal");
	  usSndDurationTotal = NODE_PSYMBOL("usSndDurationTotal");

	  pktSent       = NODE_PSYMBOL("pktSent");
	  pktRecv       = NODE_PSYMBOL("pktRecv");
	  pktSndLoss    = NODE_PSYMBOL("pktSndLoss");
	  pktRcvLoss    = NODE_PSYMBOL("pktRcvLoss");
	  pktRetrans    = NODE_PSYMBOL("pktRetrans");
	  pktSentACK    = NODE_PSYMBOL("pktSentACK");
	  pktRecvACK    = NODE_PSYMBOL("pktRecvACK");
	  pktSentNAK    = NODE_PSYMBOL("pktSentNAK");
	  pktRecvNAK    = NODE_PSYMBOL("pktRecvNAK");
	  mbpsSendRate  = NODE_PSYMBOL("mbpsSendRate");
	  mbpsRecvRate  = NODE_PSYMBOL("mbpsRecvRate");
	  usSndDuration = NODE_PSYMBOL("usSndDuration");

	  usPktSndPeriod      = NODE_PSYMBOL("usPktSndPeriod");
	  pktFlowWindow       = NODE_PSYMBOL("pktFlowWindow");
	  pktCongestionWindow = NODE_PSYMBOL("pktCongestionWindow");
	  pktFlightSize       = NODE_PSYMBOL("pktFlightSize");
	  msRTT               = NODE_PSYMBOL("msRTT");
	  mbpsBandwidth       = NODE_PSYMBOL("mbpsBandwidth");
	  byteAvailSndBuf     = NODE_PSYMBOL("byteAvailSndBuf");
	  byteAvailRcvBuf     = NODE_PSYMBOL("byteAvailRcvBuf");
  }

  jsobj->Set(msTimeStamp,        Number::New(perf.msTimeStamp));
  jsobj->Set(pktSentTotal,       Number::New(perf.pktSentTotal));
  jsobj->Set(pktRecvTotal,       Number::New(perf.pktRecvTotal));

  jsobj->Set(pktSndLossTotal,    Integer::New(perf.pktSndLossTotal));
  jsobj->Set(pktRcvLossTotal,    Integer::New(perf.pktRcvLossTotal));
  jsobj->Set(pktRetransTotal,    Integer::New(perf.pktRetransTotal));
  jsobj->Set(pktSentACKTotal,    Integer::New(perf.pktSentACKTotal));
  jsobj->Set(pktRecvACKTotal,    Integer::New(perf.pktRecvACKTotal));
  jsobj->Set(pktSentNAKTotal,    Integer::New(perf.pktSentNAKTotal));
  jsobj->Set(pktRecvNAKTotal,    Integer::New(perf.pktRecvNAKTotal));

  jsobj->Set(usSndDurationTotal,  Number::New(perf.usSndDurationTotal));

#if 0
  // local measurements
  int64_t pktSent;                     // number of sent data packets, including retransmissions
  int64_t pktRecv;                     // number of received packets
  int pktSndLoss;                      // number of lost packets (sender side)
  int pktRcvLoss;                      // number of lost packets (receiver side)
  int pktRetrans;                      // number of retransmitted packets
  int pktSentACK;                      // number of sent ACK packets
  int pktRecvACK;                      // number of received ACK packets
  int pktSentNAK;                      // number of sent NAK packets
  int pktRecvNAK;                      // number of received NAK packets
  double mbpsSendRate;                 // sending rate in Mb/s
  double mbpsRecvRate;                 // receiving rate in Mb/s
  int64_t usSndDuration;		        // busy sending time (i.e., idle time exclusive)
#endif

  jsobj->Set(pktSent,  Number::New(perf.pktSent));
  jsobj->Set(pktRecv,  Number::New(perf.pktRecv));

  jsobj->Set(pktSndLoss, Integer::New(perf.pktSndLoss));
  jsobj->Set(pktRcvLoss, Integer::New(perf.pktRcvLoss));
  jsobj->Set(pktRetrans, Integer::New(perf.pktRetrans));
  jsobj->Set(pktSentACK, Integer::New(perf.pktSentACK));
  jsobj->Set(pktRecvACK, Integer::New(perf.pktRecvACK));
  jsobj->Set(pktSentNAK, Integer::New(perf.pktSentNAK));
  jsobj->Set(pktRecvNAK, Integer::New(perf.pktRecvNAK));

  jsobj->Set(mbpsSendRate, Number::New(perf.mbpsSendRate));
  jsobj->Set(mbpsRecvRate, Number::New(perf.mbpsRecvRate));

  jsobj->Set(usSndDuration, Number::New(perf.usSndDuration));

#if 0
  // instant measurements
  double usPktSndPeriod;               // packet sending period, in microseconds
  int pktFlowWindow;                   // flow window size, in number of packets
  int pktCongestionWindow;             // congestion window size, in number of packets
  int pktFlightSize;                   // number of packets on flight
  double msRTT;                        // RTT, in milliseconds
  double mbpsBandwidth;                // estimated bandwidth, in Mb/s
  int byteAvailSndBuf;                 // available UDT sender buffer size
  int byteAvailRcvBuf;                 // available UDT receiver buffer size
#endif

  jsobj->Set(usPktSndPeriod, Number::New(perf.usPktSndPeriod));

  jsobj->Set(pktFlowWindow,       Integer::New(perf.pktFlowWindow));
  jsobj->Set(pktCongestionWindow, Integer::New(perf.pktCongestionWindow));
  jsobj->Set(pktFlightSize,       Integer::New(perf.pktFlightSize));

  jsobj->Set(msRTT,         Number::New(perf.msRTT));
  jsobj->Set(mbpsBandwidth, Number::New(perf.mbpsBandwidth));

  jsobj->Set(byteAvailSndBuf, Integer::New(perf.byteAvailSndBuf));
  jsobj->Set(byteAvailRcvBuf, Integer::New(perf.byteAvailRcvBuf));

  return scope.Close(jsobj);
}


Handle<Value> UDTWrap::SetNoDelay(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  int enable = static_cast<int>(args[0]->BooleanValue());
  int r = uv_udt_nodelay(&wrap->handle_, enable);
  if (r)
    SetErrno(uv_last_error(uv_default_loop()));

  return Undefined();
}


Handle<Value> UDTWrap::SetKeepAlive(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  int enable = args[0]->Int32Value();
  unsigned int delay = args[1]->Uint32Value();

  int r = uv_udt_keepalive(&wrap->handle_, enable, delay);
  if (r)
    SetErrno(uv_last_error(uv_default_loop()));

  return Undefined();
}


Handle<Value> UDTWrap::Bind(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  String::AsciiValue ip_address(args[0]);
  int port = args[1]->Int32Value();

  struct sockaddr_in address = uv_ip4_addr(*ip_address, port);
  int r = uv_udt_bind(&wrap->handle_, address);

  // Error starting the udt.
  if (r) SetErrno(uv_last_error(uv_default_loop()));

  return scope.Close(Integer::New(r));
}


Handle<Value> UDTWrap::Bind6(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  String::AsciiValue ip6_address(args[0]);
  int port = args[1]->Int32Value();

  struct sockaddr_in6 address = uv_ip6_addr(*ip6_address, port);
  int r = uv_udt_bind6(&wrap->handle_, address);

  // Error starting the udt.
  if (r) SetErrno(uv_last_error(uv_default_loop()));

  return scope.Close(Integer::New(r));
}


// binding UDT socket on existing udp socket/fd
Handle<Value> UDTWrap::Bindfd(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  int udpfd = args[0]->Int32Value();
#if defined(__unix__) || defined(__POSIX__) || defined(__APPLE__)

#else
  // windows int to SOCKET fd casting
  // it's valid from 0 to (INVALID_SOCKET - 1)
  if (udpfd == -1) {
      udpfd = INVALID_SOCKET;
  }
#endif

  int r = uv_udt_bindfd(&wrap->handle_, (uv_os_sock_t)udpfd);

  // Error starting the udt.
  if (r) SetErrno(uv_last_error(uv_default_loop()));

  return scope.Close(Integer::New(r));
}

// get udp/fd associated with UDT socket
Handle<Value> UDTWrap::Getudpfd(const Arguments &args) {
  HandleScope scope;
  
  UNWRAP(UDTWrap)
  
  uv_os_sock_t udpfd = -1;

  int r = uv_udt_udpfd(&wrap->handle_, &udpfd);

  // Error starting the udt.
  if (r) SetErrno(uv_last_error(uv_default_loop()));

  return scope.Close(Integer::New(udpfd));
}

Handle<Value> UDTWrap::Listen(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  int backlog = args[0]->Int32Value();

  int r = uv_listen((uv_stream_t*)&wrap->handle_, backlog, OnConnection);

  // Error starting the udt.
  if (r) SetErrno(uv_last_error(uv_default_loop()));

  return scope.Close(Integer::New(r));
}


void UDTWrap::OnConnection(uv_stream_t* handle, int status) {
  HandleScope scope;

  UDTWrap* wrap = static_cast<UDTWrap*>(handle->data);
  assert(&wrap->handle_ == (uv_udt_t*)handle);

  // We should not be getting this callback if someone as already called
  // uv_close() on the handle.
  assert(wrap->object_.IsEmpty() == false);

  Local<Value> argv[1];

  if (status == 0) {
    // Instantiate the client javascript object and handle.
    Local<Object> client_obj = Instantiate();

    // Unwrap the client javascript object.
    assert(client_obj->InternalFieldCount() > 0);
    UDTWrap* client_wrap =
        static_cast<UDTWrap*>(client_obj->GetPointerFromInternalField(0));

    if (uv_accept(handle, (uv_stream_t*)&client_wrap->handle_)) return;

    // Successful accept. Call the onconnection callback in JavaScript land.
    argv[0] = client_obj;
  } else {
    SetErrno(uv_last_error(uv_default_loop()));
    argv[0] = Local<Value>::New(Null());
  }

  MakeCallback(wrap->object_, onconnection_sym, ARRAY_SIZE(argv), argv);
}


void UDTWrap::AfterConnect(uv_connect_t* req, int status) {
  ConnectWrap* req_wrap = (ConnectWrap*) req->data;
  UDTWrap* wrap = (UDTWrap*) req->handle->data;

  HandleScope scope;

  // The wrap and request objects should still be there.
  assert(req_wrap->object_.IsEmpty() == false);
  assert(wrap->object_.IsEmpty() == false);

  if (status) {
    SetErrno(uv_last_error(uv_default_loop()));
  }

  Local<Value> argv[5] = {
    Integer::New(status),
    Local<Value>::New(wrap->object_),
    Local<Value>::New(req_wrap->object_),
    Local<Value>::New(v8::True()),
    Local<Value>::New(v8::True())
  };

  MakeCallback(req_wrap->object_, oncomplete_sym, ARRAY_SIZE(argv), argv);

  delete req_wrap;
}


Handle<Value> UDTWrap::Connect(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  String::AsciiValue ip_address(args[0]);
  int port = args[1]->Int32Value();

  struct sockaddr_in address = uv_ip4_addr(*ip_address, port);

  // I hate when people program C++ like it was C, and yet I do it too.
  // I'm too lazy to come up with the perfect class hierarchy here. Let's
  // just do some type munging.
  ConnectWrap* req_wrap = new ConnectWrap();

  int r = uv_udt_connect(&req_wrap->req_, &wrap->handle_, address,
      AfterConnect);

  req_wrap->Dispatched();

  if (r) {
    SetErrno(uv_last_error(uv_default_loop()));
    delete req_wrap;
    return scope.Close(v8::Null());
  } else {
    return scope.Close(req_wrap->object_);
  }
}


Handle<Value> UDTWrap::Connect6(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  String::AsciiValue ip_address(args[0]);
  int port = args[1]->Int32Value();

  struct sockaddr_in6 address = uv_ip6_addr(*ip_address, port);

  ConnectWrap* req_wrap = new ConnectWrap();

  int r = uv_udt_connect6(&req_wrap->req_, &wrap->handle_, address,
      AfterConnect);

  req_wrap->Dispatched();

  if (r) {
    SetErrno(uv_last_error(uv_default_loop()));
    delete req_wrap;
    return scope.Close(v8::Null());
  } else {
    return scope.Close(req_wrap->object_);
  }
}


// set socket in rendezvous mode for p2p connection
Handle<Value> UDTWrap::SetSocketRendez(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  int enable = args[0]->Int32Value();

  int r = uv_udt_setrendez(&wrap->handle_, enable);
  // Error starting the udt.
  if (r) SetErrno(uv_last_error(uv_default_loop()));

  return scope.Close(Integer::New(r));
}


// set socket Qos
Handle<Value> UDTWrap::SetSocketQos(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  int qos = args[0]->Int32Value();

  int r = uv_udt_setqos(&wrap->handle_, qos);
  // Error starting the udt.
  if (r) SetErrno(uv_last_error(uv_default_loop()));

  return scope.Close(Integer::New(r));
}


// set socket maxim bandwidth
Handle<Value> UDTWrap::SetSocketMbw(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  int mbw = args[0]->Int32Value(); // TBD... >4GBps

  int r = uv_udt_setmbw(&wrap->handle_, mbw);
  // Error starting the udt.
  if (r) SetErrno(uv_last_error(uv_default_loop()));

  return scope.Close(Integer::New(r));
}

// set socket maxim buffer size
Handle<Value> UDTWrap::SetSocketMbs(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  int mfc  = args[0]->Int32Value();
  int mudt = args[1]->Int32Value();
  int mudp = args[2]->Int32Value();

  // FC window size, UDT,UDP buffer size
  int r = uv_udt_setmbs(&wrap->handle_, mfc, mudt, mudp);
  // Error starting the udt.
  if (r) SetErrno(uv_last_error(uv_default_loop()));

  return scope.Close(Integer::New(r));
}

// set socket security mode with 128-bit session key,like(mode,k0,k1,k2,k3)
Handle<Value> UDTWrap::SetSocketSec(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  int mode = args[0]->Int32Value();
  unsigned char key[16];
  int val;
  // network byte order
  for (int i=0; i<4; i++) {
	val        = args[i+1]->Int32Value();
	key[i*4+3] = val;
	key[i*4+2] = val >> 8;
	key[i*4+1] = val >> 16;
	key[i*4+0] = val >> 24;
	///printf("val%d:0x%x ", i, val);
  }

  // security mode, 128-bit session keys
  int r = uv_udt_setsec(&wrap->handle_, mode, key, 16);
  // Error starting the udt.
  if (r) SetErrno(uv_last_error(uv_default_loop()));

  return scope.Close(Integer::New(r));
}

Handle<Value> UDTWrap::Punchhole(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  String::AsciiValue ip_address(args[0]);
  int port = args[1]->Int32Value();
  int from = args[2]->Int32Value();
  int to = args[3]->Int32Value();

  struct sockaddr_in address = uv_ip4_addr(*ip_address, port);

  int r = uv_udt_punchhole(&wrap->handle_, address, from, to);
  // Error starting the udt.
  if (r) SetErrno(uv_last_error(uv_default_loop()));

  return scope.Close(Integer::New(r));
}


Handle<Value> UDTWrap::Punchhole6(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  String::AsciiValue ip_address(args[0]);
  int port = args[1]->Int32Value();
  int from = args[2]->Int32Value();
  int to = args[3]->Int32Value();

  struct sockaddr_in6 address = uv_ip6_addr(*ip_address, port);

  int r = uv_udt_punchhole6(&wrap->handle_, address, from, to);
  // Error starting the udt.
  if (r) SetErrno(uv_last_error(uv_default_loop()));

  return scope.Close(Integer::New(r));
}


#ifdef _WIN32
static Local<Object> AddressToJS(const sockaddr* addr, const SOCKET fd, const int udtfd) {
#else
static Local<Object> AddressToJS(const sockaddr* addr, const int fd, const int udtfd) {
#endif
  static Persistent<String> address_sym;
  static Persistent<String> family_sym;
  static Persistent<String> port_sym;
  static Persistent<String> ipv4_sym;
  static Persistent<String> ipv6_sym;
  static Persistent<String> fd_sym;
  static Persistent<String> udtfd_sym;

  HandleScope scope;
  char ip[INET6_ADDRSTRLEN];
  const sockaddr_in *a4;
  const sockaddr_in6 *a6;
  int port;

  if (address_sym.IsEmpty()) {
    address_sym = NODE_PSYMBOL("address");
    family_sym = NODE_PSYMBOL("family");
    port_sym = NODE_PSYMBOL("port");
    ipv4_sym = NODE_PSYMBOL("IPv4");
    ipv6_sym = NODE_PSYMBOL("IPv6");
    fd_sym = NODE_PSYMBOL("fd");
    udtfd_sym = NODE_PSYMBOL("udtfd");
  }

  Local<Object> info = Object::New();

  switch (addr->sa_family) {
  case AF_INET6:
    a6 = reinterpret_cast<const sockaddr_in6*>(addr);
    uv_inet_ntop(AF_INET6, &a6->sin6_addr, ip, sizeof ip);
    port = ntohs(a6->sin6_port);
    info->Set(address_sym, String::New(ip));
    info->Set(family_sym, ipv6_sym);
    info->Set(port_sym, Integer::New(port));
#ifdef _WIN32
	if (fd != INVALID_SOCKET) info->Set(fd_sym, Integer::New(fd));
#else
	if (fd != -1) info->Set(fd_sym, Integer::New(fd));
#endif
    if (udtfd != -1) info->Set(udtfd_sym, Integer::New(udtfd));
    break;

  case AF_INET:
    a4 = reinterpret_cast<const sockaddr_in*>(addr);
    uv_inet_ntop(AF_INET, &a4->sin_addr, ip, sizeof ip);
    port = ntohs(a4->sin_port);
    info->Set(address_sym, String::New(ip));
    info->Set(family_sym, ipv4_sym);
    info->Set(port_sym, Integer::New(port));
#ifdef _WIN32
	if (fd != INVALID_SOCKET) info->Set(fd_sym, Integer::New(fd));
#else
	if (fd != -1) info->Set(fd_sym, Integer::New(fd));
#endif
    if (udtfd != -1) info->Set(udtfd_sym, Integer::New(udtfd));
    break;

  default:
    info->Set(address_sym, String::Empty());
  }

  return scope.Close(info);
}


}  // namespace node

NODE_MODULE(node_udt_wrap, node::UDTWrap::Initialize)
