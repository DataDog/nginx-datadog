// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache 2.0 License. This product includes software developed at
// Datadog (https://www.datadoghq.com/).
//
// Copyright 2024-Present Datadog, Inc.

#include "entrypoint.h"
#include "mocks.h"
#include "gtest/gtest.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SaveArg;

TEST(TestRegisterModule,
     TestRegistersForBeginRequestAndSendResponseNotifications) {
  MockModule module;
  MockServer server;

  EXPECT_CALL(module, SetGlobalNotifications(_, GL_APPLICATION_START |
                                                    GL_CONFIGURATION_CHANGE |
                                                    GL_APPLICATION_STOP))
      .Times(1);
  EXPECT_CALL(module, SetRequestNotifications(
                          _, RQ_SEND_RESPONSE | RQ_BEGIN_REQUEST, _))
      .Times(1);
  EXPECT_CALL(module, GetId()).Times(1);

  auto result = RegisterModule(0, &module, &server);

  EXPECT_EQ(result, S_OK);
}

TEST(TestRegisterModule, TestRegistersValidHTTPModuleFactory) {
  MockModule module;
  MockServer server;

  IHttpModuleFactory *resultFactory = NULL;
  ON_CALL(module, SetRequestNotifications)
      .WillByDefault(DoAll(SaveArg<0>(&resultFactory), Return(S_OK)));

  EXPECT_CALL(module, SetRequestNotifications).Times(1);
  EXPECT_CALL(module, SetGlobalNotifications).Times(1);
  EXPECT_CALL(module, GetId).Times(1);

  auto result = RegisterModule(0, &module, &server);

  EXPECT_EQ(result, S_OK);
  ASSERT_NE(resultFactory, nullptr);

  CHttpModule *resultFactoryOutput = NULL;
  result = resultFactory->GetHttpModule(&resultFactoryOutput, NULL);

  EXPECT_EQ(result, S_OK);
  EXPECT_NE(resultFactoryOutput, nullptr);

  resultFactory->Terminate();
}
