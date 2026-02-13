/*
 *    Copyright 2024 Datadog, Inc.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */
package com.datadog.rum.browser;

import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import java.util.function.Consumer;
import java.util.function.Function;

import static org.junit.jupiter.api.Assertions.*;

class RumConfigTest {
    private Function<String, String> getInitParameter;
    private Consumer<String> logger;

    @BeforeEach
    void setUp() {
        getInitParameter = key -> {
            switch (key) {
                case "client-token":
                    return "testClientToken";
                case "application-id":
                    return "testApplicationId";
                case "site":
                    return "testSite";
                case "service":
                    return "testService";
                case "env":
                    return "testEnv";
                case "version":
                    return "testVersion";
                case "session-sample-rate":
                    return "50";
                case "session-replay-sample-rate":
                    return "75";
                case "default-privacy-level":
                    return "testPrivacyLevel";
                default:
                    return null;
            }
        };

        logger = System.out::println;
    }

    @Test
    void testCreate() {
        RumConfig config = RumConfig.create("testFilter", getInitParameter, logger);

        assertEquals("testClientToken", config.clientToken);
        assertEquals("testApplicationId", config.applicationId);
        assertEquals("testSite", config.site);
        assertEquals("testService", config.service);
        assertEquals("testEnv", config.env);
        assertEquals("testVersion", config.version);
        assertEquals(50, config.sessionSampleRate);
        assertEquals(75, config.sessionReplaySampleRate);
        assertEquals("testPrivacyLevel", config.defaultPrivacyLevel);
    }

    @Test
    void testGetStringConfig() {
        String result = RumConfig.getStringConfig("testFilter", getInitParameter, "client-token", null, logger);
        assertEquals("testClientToken", result);
    }

    @Test
    void testGetRateConfig() {
        int result = RumConfig.getRateConfig("testFilter", getInitParameter, "session-sample-rate", 100, logger);
        assertEquals(50, result);
    }

    @Test
    void testGetRateConfigWithInvalidRate() {
        getInitParameter = key -> "200"; // invalid rate
        assertThrows(IllegalStateException.class,
                () -> RumConfig.getRateConfig("testFilter", getInitParameter, "session-sample-rate", 100, logger));
    }
}
