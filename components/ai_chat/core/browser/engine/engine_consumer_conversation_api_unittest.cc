/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/ai_chat/core/browser/engine/engine_consumer_conversation_api.h"

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "base/base64.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_writer.h"
#include "base/memory/scoped_refptr.h"
#include "base/numerics/clamped_math.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "base/test/values_test_util.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "brave/components/ai_chat/core/browser/engine/conversation_api_client.h"
#include "brave/components/ai_chat/core/browser/engine/engine_consumer.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "brave/components/ai_chat/core/common/test_utils.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;

namespace ai_chat {

namespace {
constexpr int kTestingMaxAssociatedContentLength = 100;
}

using ConversationEvent = ConversationAPIClient::ConversationEvent;

class MockConversationAPIClient : public ConversationAPIClient {
 public:
  explicit MockConversationAPIClient(const std::string& model_name)
      : ConversationAPIClient(model_name, nullptr, nullptr) {}
  ~MockConversationAPIClient() override = default;

  MOCK_METHOD(void,
              PerformRequest,
              (const std::vector<ConversationEvent>&,
               const std::string& selected_language,
               EngineConsumer::GenerationDataCallback,
               EngineConsumer::GenerationCompletedCallback),
              (override));

  std::string GetEventsJson(
      const std::vector<ConversationEvent>& conversation) {
    auto body = CreateJSONRequestBody(conversation, "", true);
    auto dict = base::test::ParseJsonDict(body);
    base::Value::List* events = dict.FindList("events");
    EXPECT_TRUE(events);
    std::string events_json;
    base::JSONWriter::WriteWithOptions(
        *events, base::JSONWriter::OPTIONS_PRETTY_PRINT, &events_json);
    return events_json;
  }
};

class EngineConsumerConversationAPIUnitTest : public testing::Test {
 public:
  EngineConsumerConversationAPIUnitTest() = default;
  ~EngineConsumerConversationAPIUnitTest() override = default;

  void SetUp() override {
    auto options = mojom::LeoModelOptions::New();
    options->display_maker = "Test Maker";
    options->name = "test-model-name";
    options->category = mojom::ModelCategory::CHAT;
    options->access = mojom::ModelAccess::BASIC;
    options->max_associated_content_length = kTestingMaxAssociatedContentLength;
    options->long_conversation_warning_character_limit = 1000;

    model_ = mojom::Model::New();
    model_->key = "test_model_key";
    model_->display_name = "Test Model Display Name";
    model_->options =
        mojom::ModelOptions::NewLeoModelOptions(std::move(options));

    engine_ = std::make_unique<EngineConsumerConversationAPI>(
        *model_->options->get_leo_model_options(), nullptr, nullptr);
    engine_->SetAPIForTesting(std::make_unique<MockConversationAPIClient>(
        model_->options->get_leo_model_options()->name));
  }

  MockConversationAPIClient* GetMockConversationAPIClient() {
    return static_cast<MockConversationAPIClient*>(engine_->GetAPIForTesting());
  }

  void TearDown() override {}

  std::string FormatComparableEventsJson(std::string_view formatted_json) {
    auto events = base::test::ParseJson(formatted_json);
    std::string events_json;
    base::JSONWriter::WriteWithOptions(
        events, base::JSONWriter::OPTIONS_PRETTY_PRINT, &events_json);
    return events_json;
  }

 protected:
  base::test::TaskEnvironment task_environment_;
  mojom::ModelPtr model_;
  std::unique_ptr<EngineConsumerConversationAPI> engine_;
};

TEST_F(EngineConsumerConversationAPIUnitTest, GenerateEvents_BasicMessage) {
  // Although these tests should likely only be testing the
  // EngineConsumerConversationAPI class, we also include testing some
  // functionality of the very related ConversationAPIClient class. Whilst
  // EngineConsumerConversationAPI merely converts from AI Chat schemas
  // such as mojom::ConversationTurn, to the Conversation API's
  // ConversationEvent, the ConversationAPIClient class also converts from
  // ConversationEvent to JSON. It's convenient to test both here but more
  // exhaustive tests of  ConversationAPIClient are performed in its own
  // unit test suite.
  std::string page_content(kTestingMaxAssociatedContentLength + 1, 'a');
  std::string expected_page_content(kTestingMaxAssociatedContentLength, 'a');
  std::string expected_user_message_content =
      "Tell the user which show is this about?";
  std::string expected_events = R"([
    {"role": "user", "type": "pageText", "content": ")" +
                                expected_page_content + R"("},
    {"role": "user", "type": "chatMessage", "content": ")" +
                                expected_user_message_content + R"("}
  ])";
  auto* mock_api_client = GetMockConversationAPIClient();
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_api_client, PerformRequest(_, _, _, _))
      .WillOnce([&](const std::vector<ConversationEvent>& conversation,
                    const std::string& selected_language,
                    EngineConsumer::GenerationDataCallback data_callback,
                    EngineConsumer::GenerationCompletedCallback callback) {
        // Some structured EXPECT calls to catch nicer errors first
        EXPECT_EQ(conversation.size(), 2u);
        EXPECT_EQ(conversation[0].role, mojom::CharacterType::HUMAN);
        // Page content should be truncated
        EXPECT_EQ(conversation[0].content, expected_page_content);
        EXPECT_EQ(conversation[0].type, ConversationAPIClient::PageText);
        EXPECT_EQ(conversation[1].role, mojom::CharacterType::HUMAN);
        // Match entire structure
        EXPECT_STREQ(mock_api_client->GetEventsJson(conversation).c_str(),
                     FormatComparableEventsJson(expected_events).c_str());
        std::move(callback).Run("");
      });

  std::vector<mojom::ConversationTurnPtr> history;
  mojom::ConversationTurnPtr turn = mojom::ConversationTurn::New();
  turn->character_type = mojom::CharacterType::HUMAN;
  turn->text = "Which show is this about?";
  turn->prompt = "Tell the user which show is this about?";
  history.push_back(std::move(turn));

  engine_->GenerateAssistantResponse(
      false, page_content, history, "", base::DoNothing(),
      base::BindLambdaForTesting(
          [&run_loop](EngineConsumer::GenerationResult) { run_loop.Quit(); }));
  run_loop.Run();
  testing::Mock::VerifyAndClearExpectations(mock_api_client);
}

TEST_F(EngineConsumerConversationAPIUnitTest, GenerateEvents_WithSelectedText) {
  std::string expected_events = R"([
    {"role": "user", "type": "pageText", "content": "This is a page about The Mandalorian."},
    {"role": "user", "type": "pageExcerpt", "content": "The Mandalorian"},
    {"role": "user", "type": "chatMessage", "content": "Is this related to a broader series?"}
  ])";
  auto* mock_api_client = GetMockConversationAPIClient();
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_api_client, PerformRequest(_, _, _, _))
      .WillOnce([&](const std::vector<ConversationEvent>& conversation,
                    const std::string& selected_language,
                    EngineConsumer::GenerationDataCallback data_callback,
                    EngineConsumer::GenerationCompletedCallback callback) {
        // Some structured EXPECT calls to catch nicer errors first
        EXPECT_EQ(conversation.size(), 3u);
        EXPECT_EQ(conversation[0].role, mojom::CharacterType::HUMAN);
        EXPECT_EQ(conversation[0].type, ConversationAPIClient::PageText);
        EXPECT_EQ(conversation[1].role, mojom::CharacterType::HUMAN);
        EXPECT_EQ(conversation[1].type, ConversationAPIClient::PageExcerpt);
        EXPECT_EQ(conversation[2].role, mojom::CharacterType::HUMAN);
        // Match entire structure
        EXPECT_STREQ(mock_api_client->GetEventsJson(conversation).c_str(),
                     FormatComparableEventsJson(expected_events).c_str());
        std::move(callback).Run("");
      });

  std::vector<mojom::ConversationTurnPtr> history;
  mojom::ConversationTurnPtr turn = mojom::ConversationTurn::New();
  turn->character_type = mojom::CharacterType::HUMAN;
  turn->text = "Is this related to a broader series?";
  turn->selected_text = "The Mandalorian";
  history.push_back(std::move(turn));

  engine_->GenerateAssistantResponse(
      false, "This is a page about The Mandalorian.", history, "",
      base::DoNothing(),
      base::BindLambdaForTesting(
          [&run_loop](EngineConsumer::GenerationResult) { run_loop.Quit(); }));
  run_loop.Run();
  testing::Mock::VerifyAndClearExpectations(mock_api_client);
}

TEST_F(EngineConsumerConversationAPIUnitTest,
       GenerateEvents_HistoryWithSelectedText) {
  // Tests events building from history with selected text and new query without
  // selected text but with page association.
  EngineConsumer::ConversationHistory history;
  history.push_back(mojom::ConversationTurn::New(
      std::nullopt, mojom::CharacterType::HUMAN, mojom::ActionType::QUERY,
      "Which show is this catchphrase from?", std::nullopt /* prompt */,
      "I have spoken.", std::nullopt, base::Time::Now(), std::nullopt,
      std::nullopt, false));
  history.push_back(mojom::ConversationTurn::New(
      std::nullopt, mojom::CharacterType::ASSISTANT,
      mojom::ActionType::RESPONSE, "The Mandalorian.",
      std::nullopt /* prompt */, std::nullopt, std::nullopt, base::Time::Now(),
      std::nullopt, std::nullopt, false));
  history.push_back(mojom::ConversationTurn::New(
      std::nullopt, mojom::CharacterType::HUMAN, mojom::ActionType::RESPONSE,
      "Is it related to a broader series?", std::nullopt /* prompt */,
      std::nullopt, std::nullopt, base::Time::Now(), std::nullopt, std::nullopt,
      false));
  std::string expected_events = R"([
    {"role": "user", "type": "pageText", "content": "This is my page. I have spoken."},
    {"role": "user", "type": "pageExcerpt", "content": "I have spoken."},
    {"role": "user", "type": "chatMessage", "content": "Which show is this catchphrase from?"},
    {"role": "assistant", "type": "chatMessage", "content": "The Mandalorian."},
    {"role": "user", "type": "chatMessage", "content": "Is it related to a broader series?"}
  ])";
  auto* mock_api_client = GetMockConversationAPIClient();
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_api_client, PerformRequest(_, _, _, _))
      .WillOnce([&](const std::vector<ConversationEvent>& conversation,
                    const std::string& selected_language,
                    EngineConsumer::GenerationDataCallback data_callback,
                    EngineConsumer::GenerationCompletedCallback callback) {
        // Some structured EXPECT calls to catch nicer errors first
        EXPECT_EQ(conversation.size(), 5u);
        EXPECT_EQ(conversation[0].role, mojom::CharacterType::HUMAN);
        EXPECT_EQ(conversation[0].type, ConversationAPIClient::PageText);
        EXPECT_EQ(conversation[1].role, mojom::CharacterType::HUMAN);
        EXPECT_EQ(conversation[2].role, mojom::CharacterType::HUMAN);
        EXPECT_EQ(conversation[3].role, mojom::CharacterType::ASSISTANT);
        EXPECT_EQ(conversation[4].role, mojom::CharacterType::HUMAN);
        // Match entire JSON
        EXPECT_STREQ(mock_api_client->GetEventsJson(conversation).c_str(),
                     FormatComparableEventsJson(expected_events).c_str());
        std::move(callback).Run("");
      });
  engine_->GenerateAssistantResponse(
      false, "This is my page. I have spoken.", history, "", base::DoNothing(),
      base::BindLambdaForTesting(
          [&run_loop](EngineConsumer::GenerationResult) { run_loop.Quit(); }));
  run_loop.Run();
  testing::Mock::VerifyAndClearExpectations(mock_api_client);
}

TEST_F(EngineConsumerConversationAPIUnitTest, GenerateEvents_Rewrite) {
  std::string expected_events = R"([
    {"role": "user", "type": "userText", "content": "Hello World"},
    {"role": "user", "type": "requestRewrite", "content": "Use a funny tone"}
  ])";
  base::RunLoop run_loop;
  auto* mock_api_client = GetMockConversationAPIClient();
  EXPECT_CALL(*mock_api_client, PerformRequest(_, _, _, _))
      .WillOnce([&](const std::vector<ConversationEvent>& conversation,
                    const std::string& selected_language,
                    EngineConsumer::GenerationDataCallback data_callback,
                    EngineConsumer::GenerationCompletedCallback callback) {
        EXPECT_EQ(conversation.size(), 2u);
        EXPECT_STREQ(mock_api_client->GetEventsJson(conversation).c_str(),
                     FormatComparableEventsJson(expected_events).c_str());
        std::move(callback).Run("");
      });

  engine_->GenerateRewriteSuggestion(
      "Hello World", "Use a funny tone", "", base::DoNothing(),
      base::BindLambdaForTesting(
          [&run_loop](EngineConsumer::GenerationResult) { run_loop.Quit(); }));
  run_loop.Run();
  testing::Mock::VerifyAndClearExpectations(mock_api_client);
}

TEST_F(EngineConsumerConversationAPIUnitTest, GenerateEvents_ModifyReply) {
  // Tests events building from history with modified agent reply.
  EngineConsumer::ConversationHistory history;
  history.push_back(mojom::ConversationTurn::New(
      std::nullopt, mojom::CharacterType::HUMAN, mojom::ActionType::QUERY,
      "Which show is 'This is the way' from?", std::nullopt /* prompt */,
      std::nullopt, std::nullopt, base::Time::Now(), std::nullopt, std::nullopt,
      false));

  std::vector<mojom::ConversationEntryEventPtr> events;
  auto search_event = mojom::ConversationEntryEvent::NewSearchStatusEvent(
      mojom::SearchStatusEvent::New());
  auto completion_event = mojom::ConversationEntryEvent::NewCompletionEvent(
      mojom::CompletionEvent::New("Mandalorian"));
  events.push_back(search_event.Clone());
  events.push_back(completion_event.Clone());

  std::vector<mojom::ConversationEntryEventPtr> modified_events;
  modified_events.push_back(search_event.Clone());
  auto modified_completion_event =
      mojom::ConversationEntryEvent::NewCompletionEvent(
          mojom::CompletionEvent::New("The Mandalorian"));
  modified_events.push_back(modified_completion_event.Clone());

  auto edit = mojom::ConversationTurn::New(
      std::nullopt, mojom::CharacterType::ASSISTANT,
      mojom::ActionType::RESPONSE, "The Mandalorian.",
      std::nullopt /* prompt */, std::nullopt, std::move(modified_events),
      base::Time::Now(), std::nullopt, std::nullopt, false);
  std::vector<mojom::ConversationTurnPtr> edits;
  edits.push_back(std::move(edit));
  history.push_back(mojom::ConversationTurn::New(
      std::nullopt, mojom::CharacterType::ASSISTANT,
      mojom::ActionType::RESPONSE, "Mandalorian.", std::nullopt /* prompt */,
      std::nullopt, std::move(events), base::Time::Now(), std::move(edits),
      std::nullopt, false));
  history.push_back(mojom::ConversationTurn::New(
      std::nullopt, mojom::CharacterType::HUMAN, mojom::ActionType::QUERY,
      "Is it related to a broader series?", std::nullopt /* prompt */,
      std::nullopt, std::nullopt, base::Time::Now(), std::nullopt, std::nullopt,
      false));
  std::string expected_events = R"([
    {"role": "user", "type": "pageText", "content": "I have spoken."},
    {"role": "user", "type": "chatMessage",
     "content": "Which show is 'This is the way' from?"},
    {"role": "assistant", "type": "chatMessage", "content": "The Mandalorian."},
    {"role": "user", "type": "chatMessage",
     "content": "Is it related to a broader series?"}
  ])";
  auto* mock_api_client = GetMockConversationAPIClient();
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_api_client, PerformRequest(_, _, _, _))
      .WillOnce([&](const std::vector<ConversationEvent>& conversation,
                    const std::string& selected_language,
                    EngineConsumer::GenerationDataCallback data_callback,
                    EngineConsumer::GenerationCompletedCallback callback) {
        // Some structured EXPECT calls to catch nicer errors first
        ASSERT_EQ(conversation.size(), 4u);
        EXPECT_EQ(conversation[0].role, mojom::CharacterType::HUMAN);
        EXPECT_EQ(conversation[0].type, ConversationAPIClient::PageText);
        EXPECT_EQ(conversation[1].role, mojom::CharacterType::HUMAN);
        EXPECT_EQ(conversation[2].role, mojom::CharacterType::ASSISTANT);
        EXPECT_EQ(conversation[3].role, mojom::CharacterType::HUMAN);
        // Match entire JSON
        EXPECT_STREQ(mock_api_client->GetEventsJson(conversation).c_str(),
                     FormatComparableEventsJson(expected_events).c_str());
        std::move(callback).Run("");
      });
  engine_->GenerateAssistantResponse(
      false, "I have spoken.", history, "", base::DoNothing(),
      base::BindLambdaForTesting(
          [&run_loop](EngineConsumer::GenerationResult) { run_loop.Quit(); }));
  run_loop.Run();
  testing::Mock::VerifyAndClearExpectations(mock_api_client);
}

TEST_F(EngineConsumerConversationAPIUnitTest, GenerateEvents_EarlyReturn) {
  EngineConsumer::ConversationHistory history;
  auto* mock_api_client = GetMockConversationAPIClient();
  auto run_loop = std::make_unique<base::RunLoop>();
  EXPECT_CALL(*mock_api_client, PerformRequest(_, _, _, _)).Times(0);
  engine_->GenerateAssistantResponse(
      false, "This is my page.", history, "", base::DoNothing(),
      base::BindLambdaForTesting(
          [&run_loop](EngineConsumer::GenerationResult result) {
            run_loop->Quit();
          }));
  run_loop->Run();
  testing::Mock::VerifyAndClearExpectations(mock_api_client);

  mojom::ConversationTurnPtr entry = mojom::ConversationTurn::New(
      std::nullopt, mojom::CharacterType::ASSISTANT,
      mojom::ActionType::RESPONSE, "", std::nullopt /* prompt */, std::nullopt,
      std::vector<mojom::ConversationEntryEventPtr>{}, base::Time::Now(),
      std::nullopt, std::nullopt, false);
  entry->events->push_back(mojom::ConversationEntryEvent::NewCompletionEvent(
      mojom::CompletionEvent::New("Me")));
  history.push_back(std::move(entry));

  EXPECT_CALL(*mock_api_client, PerformRequest(_, _, _, _)).Times(0);
  run_loop = std::make_unique<base::RunLoop>();
  engine_->GenerateAssistantResponse(
      false, "This is my page.", history, "", base::DoNothing(),
      base::BindLambdaForTesting(
          [&run_loop](EngineConsumer::GenerationResult result) {
            run_loop->Quit();
          }));
  run_loop->Run();
  testing::Mock::VerifyAndClearExpectations(mock_api_client);
}

TEST_F(EngineConsumerConversationAPIUnitTest, GenerateEvents_SummarizePage) {
  std::string expected_events = R"([
    {"role": "user", "type": "pageText", "content": "This is a sample page content."},
    {"role": "user", "type": "requestSummary", "content": ""}
  ])";
  auto* mock_api_client = GetMockConversationAPIClient();
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_api_client, PerformRequest(_, _, _, _))
      .WillOnce([&](const std::vector<ConversationEvent>& conversation,
                    const std::string& selected_language,
                    EngineConsumer::GenerationDataCallback data_callback,
                    EngineConsumer::GenerationCompletedCallback callback) {
        // Match entire structure to ensure the generated JSON is correct
        EXPECT_STREQ(mock_api_client->GetEventsJson(conversation).c_str(),
                     FormatComparableEventsJson(expected_events).c_str());
        std::move(callback).Run("");
      });
  std::vector<mojom::ConversationTurnPtr> history;
  mojom::ConversationTurnPtr turn = mojom::ConversationTurn::New();
  turn->character_type = mojom::CharacterType::HUMAN;
  turn->action_type = mojom::ActionType::SUMMARIZE_PAGE;
  turn->text =
      "Summarize the content of this page.";  // This text should be ignored
  history.push_back(std::move(turn));
  engine_->GenerateAssistantResponse(
      false, "This is a sample page content.", history, "", base::DoNothing(),
      base::BindLambdaForTesting(
          [&run_loop](EngineConsumer::GenerationResult) { run_loop.Quit(); }));
  run_loop.Run();
  testing::Mock::VerifyAndClearExpectations(mock_api_client);
}

TEST_F(EngineConsumerConversationAPIUnitTest, GenerateEvents_UploadImage) {
  auto uploaded_images = CreateSampleUploadedImages(3);
  constexpr char kTestPrompt[] = "Tell the user what is in the image?";
  constexpr char kAssistantResponse[] = "It's a lion!";
  auto* mock_api_client = GetMockConversationAPIClient();
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_api_client, PerformRequest(_, _, _, _))
      .WillOnce([&](const std::vector<ConversationEvent>& conversation,
                    const std::string& selected_language,
                    EngineConsumer::GenerationDataCallback data_callback,
                    EngineConsumer::GenerationCompletedCallback callback) {
        // Only support one image for now.
        ASSERT_EQ(conversation.size(), 2u);
        EXPECT_EQ(conversation[0].role, mojom::CharacterType::HUMAN);
        EXPECT_EQ(
            conversation[0].content,
            base::StrCat({"data:image/png;base64,",
                          base::Base64Encode(uploaded_images[0]->image_data)}));
        EXPECT_EQ(conversation[0].type, ConversationAPIClient::UploadImage);
        EXPECT_EQ(conversation[1].role, mojom::CharacterType::HUMAN);
        EXPECT_EQ(conversation[1].content, kTestPrompt);
        EXPECT_EQ(conversation[1].type, ConversationAPIClient::ChatMessage);
        std::move(callback).Run(kAssistantResponse);
      });

  std::vector<mojom::ConversationTurnPtr> history;
  history.push_back(mojom::ConversationTurn::New(
      std::nullopt, mojom::CharacterType::HUMAN, mojom::ActionType::UNSPECIFIED,
      "What is this image?", kTestPrompt, std::nullopt, std::nullopt,
      base::Time::Now(), std::nullopt, Clone(uploaded_images), false));

  base::test::TestFuture<EngineConsumer::GenerationResult> future;
  engine_->GenerateAssistantResponse(false, "", history, "", base::DoNothing(),
                                     future.GetCallback());
  EXPECT_STREQ(future.Take()->c_str(), kAssistantResponse);
  testing::Mock::VerifyAndClearExpectations(mock_api_client);
}

}  // namespace ai_chat
