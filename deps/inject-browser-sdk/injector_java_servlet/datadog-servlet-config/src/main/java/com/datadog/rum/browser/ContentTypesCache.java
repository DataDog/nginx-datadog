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

import java.util.HashMap;
import java.util.Map;

/*
 * Cache for ContentTypes answers
 */
public class ContentTypesCache {
    final static short MAX_ITEMS = 1_000;
    private final Map<String, Boolean> map;

    /*
     * Constructs a new ContentTypes cache for the provided types
     * 
     * @param contentTypes the list of ContentTypes it will store answers for
     */
    public ContentTypesCache(String[] contentTypes) {
        this.map = new HashMap<>();
        for (String contentType : contentTypes) {
            this.map.put(contentType, true);
        }
    }

    public boolean contains(String headerValue) {
        return this.map.computeIfAbsent(headerValue, this::shouldProcess);
    }

    private boolean shouldProcess(String headerValue) {
        Boolean typeAllowed = this.map.get(getContentType(headerValue));
        return typeAllowed != null && typeAllowed;
    }

    private String getContentType(String headerValue) {
        // multipart/* are expected to contain boundary unique values
        // let's abort early to avoid exploding the cache
        // additionally, if the cache is already too big, let's also abort
        if (this.map.size() > MAX_ITEMS || headerValue.startsWith("multipart/")) {
            return null;
        }
        // RFC 2045 defines optional parameters always behind a semicolon
        int semicolon = headerValue.indexOf(";");
        return semicolon != -1 ? headerValue.substring(0, semicolon) : headerValue;
    }
}
