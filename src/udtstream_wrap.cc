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

#include "udtstream_wrap.h"
#include "stream_base-inl.h"

#include "env-inl.h"
#include "handle_wrap.h"
#include "node_buffer.h"
#include "req_wrap-inl.h"
#include "util-inl.h"

#include <cstring>  // memcpy()
#include <climits>  // INT_MAX


namespace node {

using v8::Context;
using v8::DontDelete;
using v8::EscapableHandleScope;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Local;
using v8::MaybeLocal;
using v8::Object;
using v8::ReadOnly;
using v8::Signature;
using v8::Value;


void UDTStreamWrap::Initialize(Local<Object> target,
                               Local<Value> unused,
                               Local<Context> context,
                               void* priv) {
  Environment* env = Environment::GetCurrent(context);

  auto is_construct_call_callback =
      [](const FunctionCallbackInfo<Value>& args) {
    CHECK(args.IsConstructCall());
    StreamReq::ResetObject(args.This());
  };
  Local<FunctionTemplate> sw =
      FunctionTemplate::New(env->isolate(), is_construct_call_callback);
  sw->InstanceTemplate()->SetInternalFieldCount(
      StreamReq::kStreamReqField + 1 + 3);
  Local<String> wrapString =
      FIXED_ONE_BYTE_STRING(env->isolate(), "ShutdownWrap");
  sw->SetClassName(wrapString);

  // we need to set handle and callback to null,
  // so that those fields are created and functions
  // do not become megamorphic
  // Fields:
  // - oncomplete
  // - callback
  // - handle
  sw->InstanceTemplate()->Set(
      env->oncomplete_string(),
      v8::Null(env->isolate()));
  sw->InstanceTemplate()->Set(FIXED_ONE_BYTE_STRING(env->isolate(), "callback"),
      v8::Null(env->isolate()));
  sw->InstanceTemplate()->Set(FIXED_ONE_BYTE_STRING(env->isolate(), "handle"),
      v8::Null(env->isolate()));

  sw->Inherit(AsyncWrap::GetConstructorTemplate(env));

  target->Set(env->context(),
              wrapString,
              sw->GetFunction(env->context()).ToLocalChecked()).Check();
  env->set_shutdown_wrap_template(sw->InstanceTemplate());

  Local<FunctionTemplate> ww =
      FunctionTemplate::New(env->isolate(), is_construct_call_callback);
  ww->InstanceTemplate()->SetInternalFieldCount(StreamReq::kStreamReqField + 1);
  Local<String> writeWrapString =
      FIXED_ONE_BYTE_STRING(env->isolate(), "WriteWrap");
  ww->SetClassName(writeWrapString);
  ww->Inherit(AsyncWrap::GetConstructorTemplate(env));
  target->Set(env->context(),
              writeWrapString,
              ww->GetFunction(env->context()).ToLocalChecked()).Check();
  env->set_write_wrap_template(ww->InstanceTemplate());

  NODE_DEFINE_CONSTANT(target, kReadBytesOrError);
  NODE_DEFINE_CONSTANT(target, kArrayBufferOffset);
  NODE_DEFINE_CONSTANT(target, kBytesWritten);
  NODE_DEFINE_CONSTANT(target, kLastWriteWasAsync);
  target->Set(context, FIXED_ONE_BYTE_STRING(env->isolate(), "streamBaseState"),
              env->stream_base_state().GetJSArray()).Check();
}


UDTStreamWrap::UDTStreamWrap(Environment* env,
                 Local<Object> object,
                 uvudt_t* stream,
                 AsyncWrap::ProviderType provider)
    : HandleWrap(env,
                 object,
                 reinterpret_cast<uv_handle_t*>(stream),
                 provider),
      StreamBase(env),
      stream_(stream) {
  StreamBase::AttachToObject(object);
}


Local<FunctionTemplate> UDTStreamWrap::GetConstructorTemplate(
    Environment* env) {
  Local<FunctionTemplate> tmpl = env->libuv_stream_wrap_ctor_template();
  if (tmpl.IsEmpty()) {
    tmpl = env->NewFunctionTemplate(nullptr);
    tmpl->SetClassName(
        FIXED_ONE_BYTE_STRING(env->isolate(), "UDTStreamWrap"));
    tmpl->Inherit(HandleWrap::GetConstructorTemplate(env));
    tmpl->InstanceTemplate()->SetInternalFieldCount(StreamBase::kInternalFieldCount);
    Local<FunctionTemplate> get_write_queue_size =
        FunctionTemplate::New(env->isolate(),
                              GetWriteQueueSize,
                              env->as_callback_data(),
                              Signature::New(env->isolate(), tmpl));
    tmpl->PrototypeTemplate()->SetAccessorProperty(
        env->write_queue_size_string(),
        get_write_queue_size,
        Local<FunctionTemplate>(),
        static_cast<PropertyAttribute>(ReadOnly | DontDelete));
    env->SetProtoMethod(tmpl, "setBlocking", SetBlocking);
    StreamBase::AddMethods(env, tmpl);
    env->set_libuv_stream_wrap_ctor_template(tmpl);
  }
  return tmpl;
}


UDTStreamWrap* UDTStreamWrap::From(Environment* env, Local<Object> object) {
  Local<FunctionTemplate> sw = env->libuv_stream_wrap_ctor_template();
  CHECK(!sw.IsEmpty() && sw->HasInstance(object));
  return Unwrap<UDTStreamWrap>(object);
}


int UDTStreamWrap::GetFD() {
  uv_os_sock_t fd = -1;
  if (stream() != nullptr)
    uvudt_udpfd(stream(), &fd);
  return (int)fd;
}


bool UDTStreamWrap::IsAlive() {
  return HandleWrap::IsAlive(this);
}


bool UDTStreamWrap::IsClosing() {
  return uv_is_closing(reinterpret_cast<uv_handle_t*>(stream()));
}


AsyncWrap* UDTStreamWrap::GetAsyncWrap() {
  return static_cast<AsyncWrap*>(this);
}


bool UDTStreamWrap::IsIPCPipe() {
  return false;
}


int UDTStreamWrap::ReadStart() {
  return uvudt_read_start(
      stream(),
      [](uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
        static_cast<UDTStreamWrap*>(handle->data)
            ->OnUvAlloc(suggested_size, buf);
      },
      [](uvudt_t* stream, ssize_t nread, const uv_buf_t* buf) {
        static_cast<UDTStreamWrap*>(
            reinterpret_cast<uv_handle_t*>(stream)->data)
            ->OnUvRead(nread, buf);
      });
}


int UDTStreamWrap::ReadStop() {
  return uvudt_read_stop(stream());
}


void UDTStreamWrap::OnUvAlloc(size_t suggested_size, uv_buf_t* buf) {
  HandleScope scope(env()->isolate());
  Context::Scope context_scope(env()->context());

  *buf = EmitAlloc(suggested_size);
}


void UDTStreamWrap::OnUvRead(ssize_t nread, const uv_buf_t* buf) {
  HandleScope scope(env()->isolate());
  Context::Scope context_scope(env()->context());

  // We should not be getting this callback if someone has already called
  // uv_close() on the handle.
  CHECK_EQ(persistent().IsEmpty(), false);

  EmitRead(nread, *buf);
}


void UDTStreamWrap::GetWriteQueueSize(
    const FunctionCallbackInfo<Value>& info) {
  UDTStreamWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(&wrap, info.This());

  if (wrap->stream() == nullptr) {
    info.GetReturnValue().Set(0);
    return;
  }

  uint32_t write_queue_size = uvudt_get_write_queue_size(wrap->stream());
  info.GetReturnValue().Set(write_queue_size);
}


void UDTStreamWrap::SetBlocking(const FunctionCallbackInfo<Value>& args) {
  UDTStreamWrap* wrap;
  ASSIGN_OR_RETURN_UNWRAP(&wrap, args.Holder());

  CHECK_GT(args.Length(), 0);
  if (!wrap->IsAlive())
    return args.GetReturnValue().Set(UV_EINVAL);

  bool enable = args[0]->IsTrue();
  args.GetReturnValue().Set(uvudt_set_blocking(wrap->stream(), enable));
}

typedef SimpleWriteWrap<ReqWrap<uvudt_write_t>> UDTWriteWrap;

class UDTShutdownWrap : public SimpleShutdownWrap<ReqWrap<uvudt_shutdown_t>> {
public:
 UDTShutdownWrap(StreamBase* stream,
                 v8::Local<v8::Object> req_wrap_obj);

 virtual ~UDTShutdownWrap() {}

 virtual void Cancel() final {}
};

UDTShutdownWrap::UDTShutdownWrap(StreamBase* stream, 
                                 v8::Local<v8::Object> req_wrap_obj) 
    : SimpleShutdownWrap<ReqWrap<uvudt_shutdown_t>> (stream, req_wrap_obj) {}

ShutdownWrap* UDTStreamWrap::CreateShutdownWrap(Local<Object> object) {
  return new UDTShutdownWrap(this, object);
}

WriteWrap* UDTStreamWrap::CreateWriteWrap(Local<Object> object) {
  return new UDTWriteWrap(this, object);
}


int UDTStreamWrap::DoShutdown(ShutdownWrap* req_wrap_) {
  UDTShutdownWrap* req_wrap = static_cast<UDTShutdownWrap*>(req_wrap_);
  return req_wrap->Dispatch(uvudt_shutdown, stream(), AfterUvShutdown);
}


void UDTStreamWrap::AfterUvShutdown(uvudt_shutdown_t* req, int status) {
  UDTShutdownWrap* req_wrap = static_cast<UDTShutdownWrap*>(
      UDTShutdownWrap::from_req(req));
  CHECK_NOT_NULL(req_wrap);
  HandleScope scope(req_wrap->env()->isolate());
  Context::Scope context_scope(req_wrap->env()->context());
  req_wrap->Done(status);
}


// NOTE: Call to this function could change both `buf`'s and `count`'s
// values, shifting their base and decrementing their length. This is
// required in order to skip the data that was successfully written via
// uvudt_try_write().
int UDTStreamWrap::DoTryWrite(uv_buf_t** bufs, size_t* count) {
  int err;
  size_t written;
  uv_buf_t* vbufs = *bufs;
  size_t vcount = *count;

  err = uvudt_try_write(stream(), vbufs, vcount);
  if (err == UV_ENOSYS || err == UV_EAGAIN)
    return 0;
  if (err < 0)
    return err;

  // Slice off the buffers: skip all written buffers and slice the one that
  // was partially written.
  written = err;
  for (; vcount > 0; vbufs++, vcount--) {
    // Slice
    if (vbufs[0].len > written) {
      vbufs[0].base += written;
      vbufs[0].len -= written;
      written = 0;
      break;

    // Discard
    } else {
      written -= vbufs[0].len;
    }
  }

  *bufs = vbufs;
  *count = vcount;

  return 0;
}

int UDTStreamWrap::DoWrite(WriteWrap* req_wrap,
                           uv_buf_t* bufs,
                           size_t count,
                           uv_stream_t* send_handle) {
  UDTWriteWrap* w = static_cast<UDTWriteWrap*>(req_wrap);
  return w->Dispatch(uvudt_write2,
                     stream(),
                     bufs,
                     count,
                     send_handle,
                     AfterUvWrite);
}

void UDTStreamWrap::AfterUvWrite(uvudt_write_t* req, int status) {
  UDTWriteWrap* req_wrap = static_cast<UDTWriteWrap*>(
      UDTWriteWrap::from_req(req));
  CHECK_NOT_NULL(req_wrap);
  HandleScope scope(req_wrap->env()->isolate());
  Context::Scope context_scope(req_wrap->env()->context());
  req_wrap->Done(status);
}

}  // namespace node

NODE_MODULE_CONTEXT_AWARE_INTERNAL(udtstream_wrap,
                                   node::UDTStreamWrap::Initialize)
