// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "OrbitSshQt/Task.h"

#include <absl/base/macros.h>

#include <QTimer>
#include <chrono>
#include <type_traits>
#include <utility>

#include "OrbitBase/Logging.h"
#include "OrbitSsh/Error.h"
#include "OrbitSshQt/Error.h"

// Maximum time that the shutdown is allowed to take.
constexpr const std::chrono::milliseconds kShutdownTimeoutMs{2000};

namespace orbit_ssh_qt {

Task::Task(Session* session, std::string command) : session_(session), command_(command) {
  about_to_shutdown_connection_.emplace(
      QObject::connect(session_, &Session::aboutToShutdown, this, &Task::HandleSessionShutdown));
}

void Task::Start() {
  if (state_ == State::kInitial) {
    SetState(State::kNoChannel);
    OnEvent();
  }
}

void Task::Stop() {
  QTimer::singleShot(kShutdownTimeoutMs, this, [this]() {
    if (state_ < State::kChannelClosed) {
      ERROR("Task shutdown timed out");
      SetError(Error::kOrbitServiceShutdownTimedout);
    }
  });

  if (state_ == State::kCommandRunning) {
    SetState(State::kSignalEOF);
  }
  OnEvent();
}

std::string Task::ReadStdOut() {
  auto tmp = std::move(read_std_out_buffer_);
  read_std_out_buffer_ = std::string{};
  return tmp;
}

std::string Task::ReadStdErr() {
  auto tmp = std::move(read_std_err_buffer_);
  read_std_err_buffer_ = std::string{};
  return tmp;
}

void Task::Write(std::string_view data) {
  write_buffer_.append(data);
  OnEvent();
}

outcome::result<void> Task::run() {
  // read stdout
  bool added_new_data_to_read_buffer = false;
  while (true) {
    const size_t kChunkSize = 8192;
    auto result = channel_->ReadStdOut(kChunkSize);

    if (!result && !orbit_ssh::ShouldITryAgain(result)) {
      if (added_new_data_to_read_buffer) {
        emit readyReadStdOut();
      }
      return result.error();
    } else if (!result) {
      if (added_new_data_to_read_buffer) {
        emit readyReadStdOut();
      }
      break;
    } else if (result && result.value().empty()) {
      // Channel closed
      if (added_new_data_to_read_buffer) {
        emit readyReadStdOut();
      }
      SetState(State::kWaitChannelClosed);
      break;
    } else if (result) {
      read_std_out_buffer_.append(std::move(result).value());
      added_new_data_to_read_buffer = true;
    }
  }

  // read stderr
  added_new_data_to_read_buffer = false;
  while (true) {
    const size_t kChunkSize = 8192;
    auto result = channel_->ReadStdErr(kChunkSize);

    if (!result && !orbit_ssh::ShouldITryAgain(result)) {
      if (added_new_data_to_read_buffer) {
        emit readyReadStdErr();
      }
      return result.error();
    } else if (!result) {
      if (added_new_data_to_read_buffer) {
        emit readyReadStdErr();
      }
      break;
    } else if (result && result.value().empty()) {
      // Channel closed
      if (added_new_data_to_read_buffer) {
        emit readyReadStdErr();
      }
      SetState(State::kWaitChannelClosed);
      break;
    } else if (result) {
      read_std_err_buffer_.append(std::move(result).value());
      added_new_data_to_read_buffer = true;
    }
  }

  // If the state here is kWaitChannelClosed, that means a close from the remote side was detected.
  // This means writing is not possible/necessary anymore, therefore: return early.
  if (CurrentState() == State::kWaitChannelClosed) {
    return outcome::success();
  }

  // write
  if (!write_buffer_.empty()) {
    OUTCOME_TRY(auto&& result, channel_->Write(write_buffer_));
    write_buffer_ = write_buffer_.substr(result);
    emit bytesWritten(result);
  }

  return outcome::success();
}

outcome::result<void> Task::startup() {
  if (!data_event_connection_) {
    data_event_connection_.emplace(
        QObject::connect(session_, &Session::dataEvent, this, &Task::OnEvent));
  }

  switch (CurrentState()) {
    case State::kInitial:
    case State::kNoChannel: {
      auto session = session_->GetRawSession();
      if (!session) {
        return orbit_ssh::Error::kEagain;
      }

      OUTCOME_TRY(auto&& channel, orbit_ssh::Channel::OpenChannel(session_->GetRawSession()));
      channel_ = std::move(channel);
      SetState(State::kChannelInitialized);
      ABSL_FALLTHROUGH_INTENDED;
    }
    case State::kChannelInitialized: {
      OUTCOME_TRY(channel_->Exec(command_));
      SetState(State::kCommandRunning);
      break;
    }
    case State::kStarted:
    case State::kCommandRunning:
    case State::kShutdown:
    case State::kSignalEOF:
    case State::kWaitRemoteEOF:
    case State::kSignalChannelClose:
    case State::kWaitChannelClosed:
    case State::kChannelClosed:
    case State::kError:
      UNREACHABLE();
  }

  return outcome::success();
}

outcome::result<void> Task::shutdown() {
  switch (state_) {
    case State::kInitial:
    case State::kNoChannel:
    case State::kChannelInitialized:
    case State::kStarted:
    case State::kCommandRunning:
      UNREACHABLE();

    case State::kShutdown:
    case State::kSignalEOF: {
      OUTCOME_TRY(channel_->SendEOF());
      SetState(State::kWaitRemoteEOF);
      ABSL_FALLTHROUGH_INTENDED;
    }
    case State::kWaitRemoteEOF: {
      OUTCOME_TRY(channel_->WaitRemoteEOF());
      SetState(State::kSignalChannelClose);
      ABSL_FALLTHROUGH_INTENDED;
    }
    case State::kSignalChannelClose: {
      OUTCOME_TRY(channel_->Close());
      SetState(State::kWaitChannelClosed);
      ABSL_FALLTHROUGH_INTENDED;
    }
    case State::kWaitChannelClosed: {
      OUTCOME_TRY(channel_->WaitClosed());
      SetState(State::kChannelClosed);
      // The exit status is only guaranteed to be available after the channel is really closed on
      // both sides
      emit finished(channel_->GetExitStatus());
      ABSL_FALLTHROUGH_INTENDED;
    }
    case State::kChannelClosed:
      data_event_connection_ = std::nullopt;
      about_to_shutdown_connection_ = std::nullopt;
      channel_ = std::nullopt;
      break;
    case State::kError:
      UNREACHABLE();
  }

  return outcome::success();
}

void Task::SetError(std::error_code e) {
  data_event_connection_ = std::nullopt;
  about_to_shutdown_connection_ = std::nullopt;
  StateMachineHelper::SetError(e);
  channel_ = std::nullopt;
}

void Task::HandleSessionShutdown() {
  if (CurrentState() != State::kNoChannel && CurrentState() != State::kError) {
    SetError(Error::kUncleanSessionShutdown);
  }

  session_ = nullptr;
}

void Task::HandleEagain() {
  if (session_) {
    session_->HandleEagain();
  }
}

}  // namespace orbit_ssh_qt
