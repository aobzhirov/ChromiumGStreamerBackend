// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/test_runner/mock_web_speech_recognizer.h"

#include <stddef.h>

#include "base/logging.h"
#include "base/macros.h"
#include "components/test_runner/web_test_delegate.h"
#include "third_party/WebKit/public/web/WebSpeechRecognitionResult.h"
#include "third_party/WebKit/public/web/WebSpeechRecognizerClient.h"

namespace test_runner {

namespace {

// Task class for calling a client function that does not take any parameters.
typedef void (blink::WebSpeechRecognizerClient::*ClientFunctionPointer)(
    const blink::WebSpeechRecognitionHandle&);
class ClientCallTask : public MockWebSpeechRecognizer::Task {
 public:
  ClientCallTask(MockWebSpeechRecognizer* mock, ClientFunctionPointer function)
      : MockWebSpeechRecognizer::Task(mock), function_(function) {}

  ~ClientCallTask() override {}

  void run() override {
    (recognizer_->Client()->*function_)(recognizer_->Handle());
  }

 private:
  ClientFunctionPointer function_;

  DISALLOW_COPY_AND_ASSIGN(ClientCallTask);
};

// Task for delivering a result event.
class ResultTask : public MockWebSpeechRecognizer::Task {
 public:
  ResultTask(MockWebSpeechRecognizer* mock,
             const blink::WebString transcript,
             float confidence)
      : MockWebSpeechRecognizer::Task(mock),
        transcript_(transcript),
        confidence_(confidence) {}

  ~ResultTask() override {}

  void run() override {
    blink::WebVector<blink::WebString> transcripts(static_cast<size_t>(1));
    blink::WebVector<float> confidences(static_cast<size_t>(1));
    transcripts[0] = transcript_;
    confidences[0] = confidence_;
    blink::WebVector<blink::WebSpeechRecognitionResult> final_results(
        static_cast<size_t>(1));
    blink::WebVector<blink::WebSpeechRecognitionResult> interim_results;
    final_results[0].assign(transcripts, confidences, true);

    recognizer_->Client()->didReceiveResults(
        recognizer_->Handle(), final_results, interim_results);
  }

 private:
  blink::WebString transcript_;
  float confidence_;

  DISALLOW_COPY_AND_ASSIGN(ResultTask);
};

// Task for delivering a nomatch event.
class NoMatchTask : public MockWebSpeechRecognizer::Task {
 public:
  NoMatchTask(MockWebSpeechRecognizer* mock)
      : MockWebSpeechRecognizer::Task(mock) {}

  ~NoMatchTask() override {}

  void run() override {
    recognizer_->Client()->didReceiveNoMatch(
        recognizer_->Handle(), blink::WebSpeechRecognitionResult());
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(NoMatchTask);
};

// Task for delivering an error event.
class ErrorTask : public MockWebSpeechRecognizer::Task {
 public:
  ErrorTask(MockWebSpeechRecognizer* mock,
            blink::WebSpeechRecognizerClient::ErrorCode code,
            const blink::WebString& message)
      : MockWebSpeechRecognizer::Task(mock), code_(code), message_(message) {}

  ~ErrorTask() override {}

  void run() override {
    recognizer_->Client()->didReceiveError(
        recognizer_->Handle(), message_, code_);
  }

 private:
  blink::WebSpeechRecognizerClient::ErrorCode code_;
  blink::WebString message_;

  DISALLOW_COPY_AND_ASSIGN(ErrorTask);
};

// Task for tidying up after recognition task has ended.
class EndedTask : public MockWebSpeechRecognizer::Task {
 public:
  EndedTask(MockWebSpeechRecognizer* mock)
      : MockWebSpeechRecognizer::Task(mock) {}

  ~EndedTask() override {}

  void run() override {
    blink::WebSpeechRecognitionHandle handle = recognizer_->Handle();
    recognizer_->Handle().reset();
    recognizer_->Client()->didEnd(handle);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(EndedTask);
};

}  // namespace

MockWebSpeechRecognizer::MockWebSpeechRecognizer()
    : was_aborted_(false), task_queue_running_(false), delegate_(0) {
}

MockWebSpeechRecognizer::~MockWebSpeechRecognizer() {
  ClearTaskQueue();
}

void MockWebSpeechRecognizer::SetDelegate(WebTestDelegate* delegate) {
  delegate_ = delegate;
}

void MockWebSpeechRecognizer::start(
    const blink::WebSpeechRecognitionHandle& handle,
    const blink::WebSpeechRecognitionParams& params,
    blink::WebSpeechRecognizerClient* client) {
  was_aborted_ = false;
  handle_ = handle;
  client_ = client;

  task_queue_.push_back(
      new ClientCallTask(this, &blink::WebSpeechRecognizerClient::didStart));
  task_queue_.push_back(new ClientCallTask(
      this, &blink::WebSpeechRecognizerClient::didStartAudio));
  task_queue_.push_back(new ClientCallTask(
      this, &blink::WebSpeechRecognizerClient::didStartSound));

  if (!mock_transcripts_.empty()) {
    DCHECK_EQ(mock_transcripts_.size(), mock_confidences_.size());

    for (size_t i = 0; i < mock_transcripts_.size(); ++i)
      task_queue_.push_back(
          new ResultTask(this, mock_transcripts_[i], mock_confidences_[i]));

    mock_transcripts_.clear();
    mock_confidences_.clear();
  } else
    task_queue_.push_back(new NoMatchTask(this));

  task_queue_.push_back(
      new ClientCallTask(this, &blink::WebSpeechRecognizerClient::didEndSound));
  task_queue_.push_back(
      new ClientCallTask(this, &blink::WebSpeechRecognizerClient::didEndAudio));
  task_queue_.push_back(new EndedTask(this));

  StartTaskQueue();
}

void MockWebSpeechRecognizer::stop(
    const blink::WebSpeechRecognitionHandle& handle,
    blink::WebSpeechRecognizerClient* client) {
  handle_ = handle;
  client_ = client;

  // FIXME: Implement.
  NOTREACHED();
}

void MockWebSpeechRecognizer::abort(
    const blink::WebSpeechRecognitionHandle& handle,
    blink::WebSpeechRecognizerClient* client) {
  handle_ = handle;
  client_ = client;

  ClearTaskQueue();
  was_aborted_ = true;
  task_queue_.push_back(new EndedTask(this));

  StartTaskQueue();
}

void MockWebSpeechRecognizer::AddMockResult(const blink::WebString& transcript,
                                            float confidence) {
  mock_transcripts_.push_back(transcript);
  mock_confidences_.push_back(confidence);
}

void MockWebSpeechRecognizer::SetError(const blink::WebString& error,
                                       const blink::WebString& message) {
  blink::WebSpeechRecognizerClient::ErrorCode code;
  if (error == "OtherError")
    code = blink::WebSpeechRecognizerClient::OtherError;
  else if (error == "NoSpeechError")
    code = blink::WebSpeechRecognizerClient::NoSpeechError;
  else if (error == "AbortedError")
    code = blink::WebSpeechRecognizerClient::AbortedError;
  else if (error == "AudioCaptureError")
    code = blink::WebSpeechRecognizerClient::AudioCaptureError;
  else if (error == "NetworkError")
    code = blink::WebSpeechRecognizerClient::NetworkError;
  else if (error == "NotAllowedError")
    code = blink::WebSpeechRecognizerClient::NotAllowedError;
  else if (error == "ServiceNotAllowedError")
    code = blink::WebSpeechRecognizerClient::ServiceNotAllowedError;
  else if (error == "BadGrammarError")
    code = blink::WebSpeechRecognizerClient::BadGrammarError;
  else if (error == "LanguageNotSupportedError")
    code = blink::WebSpeechRecognizerClient::LanguageNotSupportedError;
  else
    return;

  ClearTaskQueue();
  task_queue_.push_back(new ErrorTask(this, code, message));
  task_queue_.push_back(new EndedTask(this));

  StartTaskQueue();
}

void MockWebSpeechRecognizer::StartTaskQueue() {
  if (task_queue_running_)
    return;
  delegate_->PostTask(new StepTask(this));
  task_queue_running_ = true;
}

void MockWebSpeechRecognizer::ClearTaskQueue() {
  while (!task_queue_.empty()) {
    delete task_queue_.front();
    task_queue_.pop_front();
  }
  task_queue_running_ = false;
}

void MockWebSpeechRecognizer::StepTask::RunIfValid() {
  if (object_->task_queue_.empty()) {
    object_->task_queue_running_ = false;
    return;
  }

  MockWebSpeechRecognizer::Task* task = object_->task_queue_.front();
  object_->task_queue_.pop_front();
  task->run();
  delete task;

  if (object_->task_queue_.empty()) {
    object_->task_queue_running_ = false;
    return;
  }

  object_->delegate_->PostTask(new StepTask(object_));
}

}  // namespace test_runner
