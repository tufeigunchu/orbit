// Copyright (c) 2021 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <absl/base/casts.h>
#include <gmock/gmock.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <gtest/gtest.h>
#include <stdint.h>

#include "CaptureFile/CaptureFile.h"
#include "CaptureFile/CaptureFileOutputStream.h"
#include "CaptureFileConstants.h"
#include "OrbitBase/TemporaryFile.h"
#include "TestUtils/TestUtils.h"

namespace orbit_capture_file {

using orbit_test_utils::HasError;
using orbit_test_utils::HasNoError;
using orbit_test_utils::HasValue;

static constexpr const char* kAnswerString =
    "Answer to the Ultimate Question of Life, The Universe, and Everything";
static constexpr const char* kNotAnAnswerString = "Some odd number, not the answer.";
static constexpr uint64_t kAnswerKey = 42;
static constexpr uint64_t kNotAnAnswerKey = 43;

using orbit_grpc_protos::ClientCaptureEvent;
using testing::HasSubstr;

static ClientCaptureEvent CreateInternedStringCaptureEvent(uint64_t key, const std::string& str) {
  ClientCaptureEvent event;
  orbit_grpc_protos::InternedString* interned_string = event.mutable_interned_string();
  interned_string->set_key(key);
  interned_string->set_intern(str);
  return event;
}

TEST(CaptureFile, CreateCaptureFileAndReadMainSection) {
  auto temporary_file_or_error = orbit_base::TemporaryFile::Create();
  ASSERT_TRUE(temporary_file_or_error.has_value()) << temporary_file_or_error.error().message();
  orbit_base::TemporaryFile temporary_file = std::move(temporary_file_or_error.value());

  std::string temp_file_name = temporary_file.file_path().string();
  temporary_file.CloseAndRemove();

  auto output_stream_or_error = CaptureFileOutputStream::Create(temp_file_name);
  ASSERT_TRUE(output_stream_or_error.has_value()) << output_stream_or_error.error().message();

  std::unique_ptr<CaptureFileOutputStream> output_stream =
      std::move(output_stream_or_error.value());

  EXPECT_TRUE(output_stream->IsOpen());

  orbit_grpc_protos::ClientCaptureEvent event1 =
      CreateInternedStringCaptureEvent(kAnswerKey, kAnswerString);
  orbit_grpc_protos::ClientCaptureEvent event2 =
      CreateInternedStringCaptureEvent(kNotAnAnswerKey, kNotAnAnswerString);

  auto write_result = output_stream->WriteCaptureEvent(event1);
  ASSERT_FALSE(write_result.has_error()) << write_result.error().message();
  write_result = output_stream->WriteCaptureEvent(event2);
  ASSERT_FALSE(write_result.has_error()) << write_result.error().message();
  auto close_result = output_stream->Close();
  ASSERT_FALSE(close_result.has_error()) << close_result.error().message();

  auto capture_file_or_error = CaptureFile::OpenForReadWrite(temporary_file.file_path());
  ASSERT_TRUE(capture_file_or_error.has_value()) << capture_file_or_error.error().message();
  std::unique_ptr<CaptureFile> capture_file = std::move(capture_file_or_error.value());

  auto capture_section = capture_file->CreateCaptureSectionInputStream();

  {
    ClientCaptureEvent event;
    ASSERT_THAT(capture_section->ReadMessage(&event), HasNoError());
    ASSERT_EQ(event.event_case(), ClientCaptureEvent::kInternedString);
    EXPECT_EQ(event.interned_string().key(), kAnswerKey);
    EXPECT_EQ(event.interned_string().intern(), kAnswerString);
  }

  {
    ClientCaptureEvent event;
    ASSERT_THAT(capture_section->ReadMessage(&event), HasNoError());
    ASSERT_EQ(event.event_case(), ClientCaptureEvent::kInternedString);
    EXPECT_EQ(event.interned_string().key(), kNotAnAnswerKey);
    EXPECT_EQ(event.interned_string().intern(), kNotAnAnswerString);
  }
}

TEST(CaptureFile, CreateCaptureFileWriteAdditionalSectionAndReadMainSection) {
  auto temporary_file_or_error = orbit_base::TemporaryFile::Create();
  ASSERT_TRUE(temporary_file_or_error.has_value()) << temporary_file_or_error.error().message();
  orbit_base::TemporaryFile temporary_file = std::move(temporary_file_or_error.value());

  std::string temp_file_name = temporary_file.file_path().string();
  temporary_file.CloseAndRemove();

  auto output_stream_or_error = CaptureFileOutputStream::Create(temp_file_name);
  ASSERT_TRUE(output_stream_or_error.has_value()) << output_stream_or_error.error().message();

  std::unique_ptr<CaptureFileOutputStream> output_stream =
      std::move(output_stream_or_error.value());

  EXPECT_TRUE(output_stream->IsOpen());

  orbit_grpc_protos::ClientCaptureEvent event1 =
      CreateInternedStringCaptureEvent(kAnswerKey, kAnswerString);
  orbit_grpc_protos::ClientCaptureEvent event2 =
      CreateInternedStringCaptureEvent(kNotAnAnswerKey, kNotAnAnswerString);

  auto write_result = output_stream->WriteCaptureEvent(event1);
  ASSERT_FALSE(write_result.has_error()) << write_result.error().message();
  write_result = output_stream->WriteCaptureEvent(event2);
  ASSERT_FALSE(write_result.has_error()) << write_result.error().message();
  auto close_result = output_stream->Close();
  ASSERT_FALSE(close_result.has_error()) << close_result.error().message();

  auto capture_file_or_error = CaptureFile::OpenForReadWrite(temporary_file.file_path());
  ASSERT_TRUE(capture_file_or_error.has_value()) << capture_file_or_error.error().message();
  std::unique_ptr<CaptureFile> capture_file = std::move(capture_file_or_error.value());

  auto section_number_or_error = capture_file->AddUserDataSection(333);
  ASSERT_TRUE(section_number_or_error.has_value()) << section_number_or_error.error().message();
  ASSERT_EQ(capture_file->GetSectionList().size(), 1);
  EXPECT_EQ(section_number_or_error.value(), 0);
  capture_file.reset();

  capture_file_or_error = CaptureFile::OpenForReadWrite(temporary_file.file_path());
  ASSERT_TRUE(capture_file_or_error.has_value()) << capture_file_or_error.error().message();
  capture_file = std::move(capture_file_or_error.value());

  auto capture_section = capture_file->CreateCaptureSectionInputStream();

  {
    ClientCaptureEvent event;
    ASSERT_THAT(capture_section->ReadMessage(&event), HasNoError());
    ASSERT_EQ(event.event_case(), ClientCaptureEvent::kInternedString);
    EXPECT_EQ(event.interned_string().key(), kAnswerKey);
    EXPECT_EQ(event.interned_string().intern(), kAnswerString);
  }

  {
    ClientCaptureEvent event;
    ASSERT_THAT(capture_section->ReadMessage(&event), HasNoError());
    ASSERT_EQ(event.event_case(), ClientCaptureEvent::kInternedString);
    EXPECT_EQ(event.interned_string().key(), kNotAnAnswerKey);
    EXPECT_EQ(event.interned_string().intern(), kNotAnAnswerString);
  }

  // Read beyond the last message to see if we are just reading zeros as
  // empty messages and then correctly return an end of section error.
  // We should not accidentally read into the next section or the section
  // list. Since section alignment is 8, we can expect at most 7 empty
  // messages, and on the 8th time we go through the loop, we must
  // encounter the end of the section.
  constexpr int kSectionAlignment = 8;
  for (int i = 0; i < kSectionAlignment; ++i) {
    ClientCaptureEvent event;
    ErrorMessageOr<void> error = capture_section->ReadMessage(&event);
    if (error.has_error()) {
      ASSERT_THAT(error, HasError("Unexpected end of section"));
      return;
    }
    ASSERT_EQ(0, event.ByteSizeLong());
  }
  FAIL() << "More empty messages at end of section than expected.";
}

TEST(CaptureFile, CreateCaptureFileAndAddSection) {
  auto temporary_file_or_error = orbit_base::TemporaryFile::Create();
  ASSERT_TRUE(temporary_file_or_error.has_value()) << temporary_file_or_error.error().message();
  orbit_base::TemporaryFile temporary_file = std::move(temporary_file_or_error.value());

  std::string temp_file_name = temporary_file.file_path().string();
  temporary_file.CloseAndRemove();

  auto output_stream_or_error = CaptureFileOutputStream::Create(temp_file_name);
  ASSERT_TRUE(output_stream_or_error.has_value()) << output_stream_or_error.error().message();

  std::unique_ptr<CaptureFileOutputStream> output_stream =
      std::move(output_stream_or_error.value());

  EXPECT_TRUE(output_stream->IsOpen());

  orbit_grpc_protos::ClientCaptureEvent event1 =
      CreateInternedStringCaptureEvent(kAnswerKey, kAnswerString);
  orbit_grpc_protos::ClientCaptureEvent event2 =
      CreateInternedStringCaptureEvent(kNotAnAnswerKey, kNotAnAnswerString);

  auto write_result = output_stream->WriteCaptureEvent(event1);
  ASSERT_FALSE(write_result.has_error()) << write_result.error().message();
  write_result = output_stream->WriteCaptureEvent(event2);
  ASSERT_FALSE(write_result.has_error()) << write_result.error().message();
  auto close_result = output_stream->Close();
  ASSERT_FALSE(close_result.has_error()) << close_result.error().message();

  auto capture_file_or_error = CaptureFile::OpenForReadWrite(temporary_file.file_path());
  ASSERT_TRUE(capture_file_or_error.has_value()) << capture_file_or_error.error().message();
  std::unique_ptr<CaptureFile> capture_file = std::move(capture_file_or_error.value());
  EXPECT_EQ(capture_file->GetSectionList().size(), 0);

  uint64_t buf_size;
  {
    ClientCaptureEvent event;
    event.mutable_capture_finished()->set_status(orbit_grpc_protos::CaptureFinished::kFailed);
    event.mutable_capture_finished()->set_error_message("some error");

    buf_size = event.ByteSizeLong() +
               google::protobuf::io::CodedOutputStream::VarintSize32(event.ByteSizeLong());
    auto buf = make_unique_for_overwrite<uint8_t[]>(buf_size);
    google::protobuf::io::ArrayOutputStream array_output_stream(buf.get(), buf_size);
    google::protobuf::io::CodedOutputStream coded_output_stream(&array_output_stream);
    coded_output_stream.WriteVarint32(event.ByteSizeLong());
    ASSERT_TRUE(event.SerializeToCodedStream(&coded_output_stream));
    auto section_number_or_error = capture_file->AddUserDataSection(buf_size);
    ASSERT_THAT(section_number_or_error, HasValue());
    ASSERT_EQ(capture_file->GetSectionList().size(), 1);
    EXPECT_EQ(section_number_or_error.value(), 0);

    EXPECT_EQ(capture_file->FindSectionByType(kSectionTypeUserData), 0);
    // Write something to the section
    std::string something{"something"};
    constexpr uint64_t kOffsetInSection = 5;
    auto write_to_section_result =
        capture_file->WriteToSection(0, kOffsetInSection, something.c_str(), something.size());
    ASSERT_THAT(write_to_section_result, HasNoError());

    {
      std::string content;
      content.resize(something.size());
      auto read_result =
          capture_file->ReadFromSection(0, kOffsetInSection, content.data(), something.size());
      ASSERT_THAT(read_result, HasNoError());
      EXPECT_EQ(content, something);
    }

    ASSERT_THAT(
        capture_file->WriteToSection(section_number_or_error.value(), 0, buf.get(), buf_size),
        HasNoError());
  }

  {
    const auto& capture_file_section = capture_file->GetSectionList()[0];
    EXPECT_EQ(capture_file_section.size, buf_size);
    EXPECT_GT(capture_file_section.offset, 0);
    EXPECT_EQ(capture_file_section.type, kSectionTypeUserData);
  }

  // Reopen the file to make sure this information was saved
  capture_file.reset();

  capture_file_or_error = CaptureFile::OpenForReadWrite(temporary_file.file_path());
  ASSERT_TRUE(capture_file_or_error.has_value()) << capture_file_or_error.error().message();
  capture_file = std::move(capture_file_or_error.value());
  EXPECT_EQ(capture_file->GetSectionList().size(), 1);
  {
    const auto& capture_file_section = capture_file->GetSectionList()[0];
    EXPECT_EQ(capture_file_section.type, kSectionTypeUserData);
    EXPECT_GT(capture_file_section.offset, 0);
    EXPECT_EQ(capture_file_section.size, buf_size);
  }

  ASSERT_EQ(capture_file->FindSectionByType(kSectionTypeUserData), 0);

  {
    auto section_input_stream = capture_file->CreateProtoSectionInputStream(0);
    ASSERT_NE(section_input_stream.get(), nullptr);
    ClientCaptureEvent event_from_file;
    ASSERT_THAT(section_input_stream->ReadMessage(&event_from_file), HasNoError());
    EXPECT_EQ(event_from_file.capture_finished().status(),
              orbit_grpc_protos::CaptureFinished::kFailed);
    EXPECT_EQ(event_from_file.capture_finished().error_message(), "some error");
  }
}

TEST(CaptureFile, OpenCaptureFileInvalidSignature) {
  auto temporary_file_or_error = orbit_base::TemporaryFile::Create();
  ASSERT_TRUE(temporary_file_or_error.has_value()) << temporary_file_or_error.error().message();
  orbit_base::TemporaryFile temporary_file = std::move(temporary_file_or_error.value());

  auto write_result =
      orbit_base::WriteFully(temporary_file.fd(), "This is not an Orbit Capture File");

  auto capture_file_or_error = CaptureFile::OpenForReadWrite(temporary_file.file_path());
  ASSERT_TRUE(capture_file_or_error.has_error());
  EXPECT_EQ(capture_file_or_error.error().message(), "Invalid file signature");
}

TEST(CaptureFile, OpenCaptureFileTooSmall) {
  auto temporary_file_or_error = orbit_base::TemporaryFile::Create();
  ASSERT_TRUE(temporary_file_or_error.has_value()) << temporary_file_or_error.error().message();
  orbit_base::TemporaryFile temporary_file = std::move(temporary_file_or_error.value());

  auto write_result = orbit_base::WriteFully(temporary_file.fd(), "ups");

  auto capture_file_or_error = CaptureFile::OpenForReadWrite(temporary_file.file_path());
  ASSERT_TRUE(capture_file_or_error.has_error());
  EXPECT_EQ(capture_file_or_error.error().message(), "Not enough bytes left in the file: 3 < 24");
}

static std::string CreateHeader(uint32_t version, uint64_t capture_section_offset,
                                uint64_t section_list_offset) {
  std::string header{"ORBT", 4};
  header.append(std::string_view{absl::bit_cast<const char*>(&version), sizeof(version)});
  header.append(std::string_view{absl::bit_cast<const char*>(&capture_section_offset),
                                 sizeof(capture_section_offset)});
  header.append(std::string_view{absl::bit_cast<const char*>(&section_list_offset),
                                 sizeof(section_list_offset)});

  return header;
}

TEST(CaptureFile, OpenCaptureFileInvalidVersion) {
  auto temporary_file_or_error = orbit_base::TemporaryFile::Create();
  ASSERT_TRUE(temporary_file_or_error.has_value()) << temporary_file_or_error.error().message();
  orbit_base::TemporaryFile temporary_file = std::move(temporary_file_or_error.value());

  std::string header = CreateHeader(0, 0, 0);

  auto write_result = orbit_base::WriteFully(temporary_file.fd(), header);

  auto capture_file_or_error = CaptureFile::OpenForReadWrite(temporary_file.file_path());
  EXPECT_THAT(capture_file_or_error, HasError("Incompatible version 0, expected 1"));
}

TEST(CaptureFile, OpenCaptureFileInvalidSectionListSize) {
  auto temporary_file_or_error = orbit_base::TemporaryFile::Create();
  ASSERT_TRUE(temporary_file_or_error.has_value()) << temporary_file_or_error.error().message();
  orbit_base::TemporaryFile temporary_file = std::move(temporary_file_or_error.value());

  std::string header = CreateHeader(1, 24, 32);
  header.append(std::string_view{"12345678", 8});
  constexpr uint64_t kSectionListSize = 10;
  header.append(
      std::string_view{absl::bit_cast<const char*>(&kSectionListSize), sizeof(kSectionListSize)});

  auto write_result = orbit_base::WriteFully(temporary_file.fd(), header);

  auto capture_file_or_error = CaptureFile::OpenForReadWrite(temporary_file.file_path());
  EXPECT_THAT(capture_file_or_error, HasError("Unexpected EOF while reading section list"));
}

TEST(CaptureFile, OpenCaptureFileInvalidSectionListSizeTooLarge) {
  auto temporary_file_or_error = orbit_base::TemporaryFile::Create();
  ASSERT_TRUE(temporary_file_or_error.has_value()) << temporary_file_or_error.error().message();
  orbit_base::TemporaryFile temporary_file = std::move(temporary_file_or_error.value());

  std::string header = CreateHeader(1, 24, 32);
  header.append(std::string_view{"12345678", 8});
  constexpr uint64_t kSectionListSize = 65'536;
  header.append(
      std::string_view{absl::bit_cast<const char*>(&kSectionListSize), sizeof(kSectionListSize)});

  auto write_result = orbit_base::WriteFully(temporary_file.fd(), header);

  auto capture_file_or_error = CaptureFile::OpenForReadWrite(temporary_file.file_path());
  EXPECT_THAT(capture_file_or_error, HasError("The section list is too large"));
}

}  // namespace orbit_capture_file