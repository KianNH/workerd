// Copyright (c) 2017-2022 Cloudflare, Inc.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

#pragma once

#include <kj/debug.h>

#include <workerd/io/worker-interface.capnp.h>
#include <workerd/api/global-scope.h>
#include <workerd/io/worker-interface.h>

namespace workerd::api {

class HibernatableWebSocketMessageEvent final: public ExtendableEvent {
public:
  explicit HibernatableWebSocketMessageEvent(v8::Isolate* isolate, rpc::EventDispatcher::HibernatableWebSocketMessageParams::Reader params);

  static jsg::Ref<HibernatableWebSocketMessageEvent> constructor(kj::String type) = delete;

  JSG_RESOURCE_TYPE(HibernatableWebSocketMessageEvent) {
      JSG_INHERIT(ExtendableEvent);
  }
};

class HibernatableWebSocketMessageController final: public jsg::Object {
  // Type used when calling a module-exported HibernatableWebSocketMessage event handler.
public:
  HibernatableWebSocketMessageController(jsg::Ref<HibernatableWebSocketMessageEvent> event)
      : event(kj::mv(event)) {}


  JSG_RESOURCE_TYPE(HibernatableWebSocketMessageController) {
  }

private:
  jsg::Ref<HibernatableWebSocketMessageEvent> event;
};

struct HibernatableWebSocketMessageExportedHandler {
  // Extension of ExportedHandler covering HibernatableWebSocketMessage handers.

  typedef kj::Promise<void> HibernatableWebSocketMessageHandler(
      jsg::Ref<HibernatableWebSocketMessageController> controller, jsg::Value env, jsg::Optional<jsg::Ref<ExecutionContext>> ctx);
  jsg::LenientOptional<jsg::Function<HibernatableWebSocketMessageHandler>> message;

  JSG_STRUCT(message);
};

class HibernatableWebSocketMessageCustomEventImpl final: public WorkerInterface::CustomEvent, public kj::Refcounted {
public:
  HibernatableWebSocketMessageCustomEventImpl(
      uint16_t typeId, rpc::EventDispatcher::HibernatableWebSocketMessageParams::Reader params)
    : typeId(typeId), params(params) {}

  kj::Promise<Result> run(
      kj::Own<IoContext_IncomingRequest> incomingRequest,
      kj::Maybe<kj::StringPtr> entrypointName) override;

  kj::Promise<Result> sendRpc(
      capnp::HttpOverCapnpFactory& httpOverCapnpFactory,
      capnp::ByteStreamFactory& byteStreamFactory,
      kj::TaskSet& waitUntilTasks,
      rpc::EventDispatcher::Client dispatcher) override;

  uint16_t getType() override {
    return typeId;
  }
private:
  uint16_t typeId;
  rpc::EventDispatcher::HibernatableWebSocketMessageParams::Reader params;
};

#define EW_WEB_SOCKET_MESSAGE_ISOLATE_TYPES      \
  api::HibernatableWebSocketMessageEvent,       \
  api::HibernatableWebSocketMessageController,  \
  api::HibernatableWebSocketMessageExportedHandler
}  // namespace workerd::api
