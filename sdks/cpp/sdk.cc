// Copyright 2017 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <future>
#include "sdk.h"
#include "sdk.pb.h"

#define HEALTHCHECK_TIMEOUT_SECONDS 10 // externally obtained
namespace agones {

    const int port = 59357;

    SDK::SDK(const std::function<bool()> onHealthCheck) :
        m_shutShutdown(false) {
        if (onHealthCheck == nullptr)
        {
            m_onHealthCheck = std::bind(&SDK::DefaultHealthCheck, this);
        }
        else
        {
            m_onHealthCheck = onHealthCheck;
        }

        channel = grpc::CreateChannel("localhost:" + std::to_string(port), grpc::InsecureChannelCredentials());
    }

    bool SDK::Connect() {
        if (!channel->WaitForConnected(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_seconds(30, GPR_TIMESPAN)))) {
            return false;
        }

        stub = stable::agones::dev::sdk::SDK::NewStub(channel);

        // make the health connection
        stable::agones::dev::sdk::Empty response;
        health = stub->Health(new grpc::ClientContext(), &response);

        return true;
    }

    grpc::Status SDK::Ready() {
        std::thread t([&]() {
            // call health immediately on ready, or wait sleep seconds?
            while (!m_shutShutdown) {
                std::future<bool> future = std::async(std::launch::async, m_onHealthCheck);
                std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
                // not sure what the logic should be here, if we timeout waiting for the callback, should we retry immediately, or sleep the 
                // defined amount of seconds (which would result in timeout*2)
                long sleepSeconds = 0;
                if (std::future_status::ready == future.wait_until(now + std::chrono::seconds(HEALTHCHECK_TIMEOUT_SECONDS)))
                {
                    sleepSeconds = HEALTHCHECK_TIMEOUT_SECONDS - std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - now).count();
                    Health(); // Health(future.get());
                }
                else
                {
                    // probably warn here
                    Health(); // false
                }

                std::this_thread::sleep_for(std::chrono::seconds(sleepSeconds));
            }
        });
        t.detach();
        grpc::ClientContext *context = new grpc::ClientContext();
        context->set_deadline(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_seconds(30, GPR_TIMESPAN)));
        stable::agones::dev::sdk::Empty request;
        stable::agones::dev::sdk::Empty response;

        return stub->Ready(context, request, &response);
    }

    bool SDK::Health() {
        stable::agones::dev::sdk::Empty request;
        return health->Write(request);
    }

    grpc::Status SDK::Shutdown() {
        grpc::ClientContext *context = new grpc::ClientContext();
        context->set_deadline(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_seconds(30, GPR_TIMESPAN)));
        stable::agones::dev::sdk::Empty request;
        stable::agones::dev::sdk::Empty response;

        return stub->Shutdown(context, request, &response);
    }
}