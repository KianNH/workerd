// Copyright (c) 2017-2022 Cloudflare, Inc.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

#include "hibernatable-web-socket.h"

namespace workerd::api {

kj::Promise<WorkerInterface::CustomEvent::Result> HibernatableWebSocketMessageCustomEventImpl::run(
    kj::Own<IoContext_IncomingRequest> incomingRequest,
    kj::Maybe<kj::StringPtr> entrypointName) {

  auto& context = incomingRequest->getContext();

  incomingRequest->delivered();

  auto message = params.getMessage().getSendText();
  KJ_DBG(message);

  // todo(now): Interact with websocket manager.
// todo(now): HibernatableWebSocket rename


  auto completed = co_await incomingRequest->finishScheduled();
  incomingRequest->drain();
  co_return workerd::WorkerInterface::CustomEvent::Result {
    .outcome = completed ? context.waitUntilStatus() : EventOutcome::EXCEEDED_CPU,
  };
}

kj::Promise<WorkerInterface::CustomEvent::Result> HibernatableWebSocketMessageCustomEventImpl::sendRpc(
    capnp::HttpOverCapnpFactory& httpOverCapnpFactory,
    capnp::ByteStreamFactory& byteStreamFactory,
    kj::TaskSet& waitUntilTasks,
    rpc::EventDispatcher::Client dispatcher) {
  auto req = dispatcher.hibernatableWebSocketMessageRequest();
  req.setMessage(params.getMessage());
  return req.send().then([](auto resp) {
    auto respResult = resp.getResult();
    return Result {
      .outcome = respResult.getOutcome(),
    };
  });
}

}  // namespace workerd::api
