/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_BRAVE_REWARDS_CORE_ENGINE_GEMINI_GEMINI_H_
#define BRAVE_COMPONENTS_BRAVE_REWARDS_CORE_ENGINE_GEMINI_GEMINI_H_

#include <string>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ref.h"
#include "brave/components/brave_rewards/core/engine/endpoint/gemini/gemini_server.h"
#include "brave/components/brave_rewards/core/engine/wallet_provider/wallet_provider.h"
#include "brave/components/brave_rewards/core/mojom/rewards.mojom.h"

namespace brave_rewards::internal {
class RewardsEngine;

namespace gemini {

class Gemini final : public wallet_provider::WalletProvider {
 public:
  explicit Gemini(RewardsEngine& engine);

  const char* WalletType() const override;

  void AssignWalletLinks(mojom::ExternalWallet& external_wallet) override;

  void FetchBalance(
      base::OnceCallback<void(mojom::Result, double)> callback) override;

  std::string GetFeeAddress() const override;

  base::TimeDelta GetDelay() const override;

 private:
  endpoint::GeminiServer server_;
};

}  // namespace gemini
}  // namespace brave_rewards::internal

#endif  // BRAVE_COMPONENTS_BRAVE_REWARDS_CORE_ENGINE_GEMINI_GEMINI_H_
