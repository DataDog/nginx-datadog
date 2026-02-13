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

import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;

import org.sitemesh.webapp.contentfilter.ResponseMetaData;

import javax.servlet.FilterConfig;
import javax.servlet.ServletContext;
import javax.servlet.ServletException;
import javax.servlet.http.HttpServletRequest;
import javax.servlet.http.HttpServletResponse;
import java.nio.CharBuffer;
import java.io.IOException;
import java.io.PrintWriter;
import java.io.StringWriter;

import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.*;

class DatadogBrowserFilterTest {

    @Mock
    private HttpServletRequest request;

    @Mock
    private HttpServletResponse response;

    @Mock
    private ResponseMetaData responseMetaData;

    @Mock
    private FilterConfig filterConfig;

    @Mock
    private ServletContext servletContext;

    private DatadogBrowserFilter filter;
    private StringWriter stringWriter;

    @BeforeAll
    static void setUpAll() {
        mockStatic(RumConfig.class);
    }

    @BeforeEach
    void setUp() {
        MockitoAnnotations.openMocks(this);
        filter = new DatadogBrowserFilter();
        stringWriter = new StringWriter();

        when(filterConfig.getServletContext()).thenReturn(servletContext);
        when(filterConfig.getInitParameter(anyString())).thenReturn("");
        when(RumConfig.getScriptTag(anyString(), any(), any())).thenReturn("#SCRIPT_TAG#");

        try {
            when(response.getWriter()).thenReturn(new PrintWriter(stringWriter));
            // Calling the init method
            filter.init(filterConfig);
        } catch (Exception e) {
            fail("Exception should not be thrown", e);
        }
    }

    @Test
    void testPostProcessWithNullInjectedScriptTag() throws IOException, ServletException {
        CharBuffer buffer = CharBuffer.wrap("Some content");
        assertFalse(filter.postProcess("text/html", buffer, request, response, responseMetaData));
    }

    @Test
    void testPostProcessWithUnsupportedContentType() throws IOException, ServletException {
        CharBuffer buffer = CharBuffer.wrap("Some content");
        assertFalse(filter.postProcess("application/xml", buffer, request, response, responseMetaData));
    }

    @Test
    void testPostProcessWithoutClosingHeadTag() throws IOException, ServletException {
        CharBuffer buffer = CharBuffer.wrap("<html><head>Some content<body>Some body</body></html>");
        assertFalse(filter.postProcess("text/html", buffer, request, response, responseMetaData));
    }

    @Test
    void testPostProcessWithContentLengthHeader() throws IOException, ServletException {
        CharBuffer buffer = CharBuffer.wrap("<html><head>Some content</head></html>");
        when(response.containsHeader("Content-Length")).thenReturn(true);
        assertTrue(filter.postProcess("text/html", buffer, request, response, responseMetaData));
        assertEquals("<html><head>Some content#SCRIPT_TAG#</head></html>", stringWriter.toString());
        verify(response).setContentLength(-1);
    }

    @Test
    void testPostProcessWithSuccessfulInjectionOnHtml() throws IOException, ServletException {
        CharBuffer buffer = CharBuffer.wrap("<html><head>Some content</head></html>");
        assertTrue(filter.postProcess("text/html", buffer, request, response, responseMetaData));
        assertEquals("<html><head>Some content#SCRIPT_TAG#</head></html>", stringWriter.toString());
    }

    @Test
    void testPostProcessWithSuccessfulInjectionOnXhtml() throws IOException, ServletException {
        CharBuffer buffer = CharBuffer.wrap("<html><head>Some content</head></html>");
        assertTrue(filter.postProcess("application/xhtml+xml", buffer, request, response, responseMetaData));
        assertEquals("<html><head>Some content#SCRIPT_TAG#</head></html>", stringWriter.toString());
    }

    @Test
    void testPostProcessWithSuccessfulInjectionOnHtmlWithCharset() throws IOException, ServletException {
        CharBuffer buffer = CharBuffer.wrap("<html><head>Some content</head></html>");
        assertTrue(filter.postProcess("text/html;charset=UTF-8", buffer, request, response, responseMetaData));
        assertEquals("<html><head>Some content#SCRIPT_TAG#</head></html>", stringWriter.toString());
    }

    @Test
    void testPostProcessWithSuccessfulInjectionOnHtmlWithTwoHeadTags() throws IOException, ServletException {
        CharBuffer buffer = CharBuffer.wrap("<html><head>Some content</head><head>Some content</head></html>");
        assertTrue(filter.postProcess("text/html", buffer, request, response, responseMetaData));
        assertEquals("<html><head>Some content#SCRIPT_TAG#</head><head>Some content</head></html>",
                stringWriter.toString());
    }
}
