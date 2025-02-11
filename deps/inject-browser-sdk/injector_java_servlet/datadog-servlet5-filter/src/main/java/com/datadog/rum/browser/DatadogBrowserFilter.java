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

import org.sitemesh.webapp.contentfilter.BasicSelector;
import org.sitemesh.webapp.contentfilter.ContentBufferingFilter;
import org.sitemesh.webapp.contentfilter.ResponseMetaData;

import jakarta.servlet.FilterConfig;
import jakarta.servlet.ServletException;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import java.io.IOException;
import java.nio.CharBuffer;

public class DatadogBrowserFilter extends ContentBufferingFilter {
    private static final String[] VALID_CONTENT_TYPES = new String[] { "text/html", "application/xhtml+xml" };
    private String injectedScriptTag;

    private ContentTypesCache contentTypesCache;

    public DatadogBrowserFilter() {
        super(new BasicSelector(true, VALID_CONTENT_TYPES));
        this.contentTypesCache = new ContentTypesCache(VALID_CONTENT_TYPES);
    }

    @Override
    public void init(FilterConfig filterConfig) throws ServletException {
        super.init(filterConfig);
        try {
            this.injectedScriptTag = RumConfig.getScriptTag(DatadogBrowserFilter.class.getSimpleName(),
                    filterConfig::getInitParameter, filterConfig.getServletContext()::log);
        } catch (IllegalStateException e) {
            filterConfig.getServletContext().log("Failed to initialize DatadogBrowserFilter", e);
        }
    }

    @Override
    protected boolean postProcess(String contentType, CharBuffer buffer, HttpServletRequest request,
            HttpServletResponse response, ResponseMetaData responseMetaData) throws IOException, ServletException {
        // Check injected script tag was initialized
        if (this.injectedScriptTag == null) {
            return false;
        }

        // Check response content-type is selected for processing
        if (!this.contentTypesCache.contains(contentType)) {
            return false;
        }

        // Pull buffer string and look for head tag close
        String text = buffer.toString();
        int index = text.indexOf("</head>");
        if (index == -1) {
            return false;
        }

        // Some servlet container's (Tomcat >8.5) will set the content length to the
        // size of the decorator if it is a static file. Check if content length has
        // already been set and if so, clear it.
        if (response.containsHeader("Content-Length")) {
            response.setContentLength(-1);
        }

        // Inject script tags
        text = text.substring(0, index) + this.injectedScriptTag + text.substring(index);
        // Use updated text as response
        response.getWriter().print(text);
        return true;
    }
}
