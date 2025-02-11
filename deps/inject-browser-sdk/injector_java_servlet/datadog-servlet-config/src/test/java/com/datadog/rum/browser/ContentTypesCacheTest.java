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
import static org.junit.jupiter.api.Assertions.*;

import java.util.Arrays;

class ContentTypesCacheTest {
    private ContentTypesCache cache;

    @BeforeEach
    void setUp() {
        String[] contentTypes = { "text/html", "application/json", "text/css" };
        cache = new ContentTypesCache(contentTypes);
    }

    @Test
    void testContains() {
        assertTrue(cache.contains("text/html"));
        assertTrue(cache.contains("application/json"));
        assertTrue(cache.contains("text/css"));
        assertFalse(cache.contains("application/xml"));
    }

    @Test
    void testContainsWithParameters() {
        assertTrue(cache.contains("text/html; charset=UTF-8"));
        assertTrue(cache.contains("application/json; charset=UTF-8"));
        assertTrue(cache.contains("text/css; charset=UTF-8"));
        assertFalse(cache.contains("application/xml; charset=UTF-8"));
    }

    @Test
    void testGetContentType() {
        try {
            java.lang.reflect.Method method = ContentTypesCache.class.getDeclaredMethod("getContentType", String.class);
            method.setAccessible(true);

            assertEquals("text/html", method.invoke(cache, "text/html; charset=UTF-8"));
            assertEquals("application/json", method.invoke(cache, "application/json; charset=UTF-8"));
            assertEquals("text/css", method.invoke(cache, "text/css; charset=UTF-8"));
            assertEquals("application/xml", method.invoke(cache, "application/xml; charset=UTF-8"));
        } catch (Exception e) {
            fail("Exception should not be thrown");
        }
    }

    @Test
    void testGetContentTypeWithLargeCache() {
        String[] htmlType = { "text/html" };
        String[] maxContentTypes = new String[ContentTypesCache.MAX_ITEMS];
        Arrays.fill(maxContentTypes, "text/html" + Math.random());
        String[] contentTypes = new String[ContentTypesCache.MAX_ITEMS + 1];
        System.arraycopy(htmlType, 0, contentTypes, 0, 1);
        System.arraycopy(maxContentTypes, 0, contentTypes, 1, maxContentTypes.length);
        ContentTypesCache cache = new ContentTypesCache(contentTypes);
        
        assertFalse(cache.contains("foo/bar"));
        assertTrue(cache.contains("text/html"));
    }
}
