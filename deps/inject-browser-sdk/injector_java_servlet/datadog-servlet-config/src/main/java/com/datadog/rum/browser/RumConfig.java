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

import java.util.function.Consumer;
import java.util.function.Function;

public class RumConfig {
  private static final String DEFAULT_RATE = "DEFAULT_RATE";
  public final String clientToken;
  public final String applicationId;
  public final String site;
  public final String service;
  public final String env;
  public final String version;
  public final String cdn_region = "us1";
  public final String rum_version = "v5";
  public final int sessionSampleRate;
  public final int sessionReplaySampleRate;
  public final String defaultPrivacyLevel;

  private RumConfig(
      String clientToken,
      String applicationId,
      String site,
      String service,
      String env,
      String version,
      int sessionSampleRate,
      int sessionReplaySampleRate,
      String defaultPrivacyLevel) {
    this.clientToken = clientToken;
    this.applicationId = applicationId;
    this.site = site;
    this.service = service;
    this.env = env;
    this.version = version;
    this.sessionSampleRate = sessionSampleRate;
    this.sessionReplaySampleRate = sessionReplaySampleRate;
    this.defaultPrivacyLevel = defaultPrivacyLevel;
  }

  public static RumConfig create(String filterName, Function<String, String> getInitParameter,
      Consumer<String> logger) {
    return new RumConfig(
        getStringConfig(filterName, getInitParameter, "client-token", null, logger),
        getStringConfig(filterName, getInitParameter, "application-id", null, logger),
        getStringConfig(filterName, getInitParameter, "site", "datadoghq.com", logger),
        getStringConfig(filterName, getInitParameter, "service", "", logger),
        getStringConfig(filterName, getInitParameter, "env", "", logger),
        getStringConfig(filterName, getInitParameter, "version", "", logger),
        getRateConfig(filterName, getInitParameter, "session-sample-rate", 100, logger),
        getRateConfig(filterName, getInitParameter, "session-replay-sample-rate", 100, logger),
        getStringConfig(filterName, getInitParameter, "default-privacy-level", "mask-user-input", logger));
  }

  public static String getScriptTag(String filterName, Function<String, String> getInitParameter,
      Consumer<String> logger) {
    RumConfig config = RumConfig.create(filterName, getInitParameter, logger);
    return "<script>\n" +
        "  (function(h,o,u,n,d) {\n" +
        "    h=h[d]=h[d]||{q:[],onReady:function(c){h.q.push(c)}}\n" +
        "    d=o.createElement(u);d.async=1;d.src=n\n" +
        "    n=o.getElementsByTagName(u)[0];n.parentNode.insertBefore(d,n)\n" +
        "  })(window,document,'script','https://www.datadoghq-browser-agent.com/" + config.cdn_region + "/"
        + config.rum_version + "/datadog-rum.js','DD_RUM')\n" +
        "  window.DD_RUM.onReady(function() {\n" +
        "    window.DD_RUM.init({\n" +
        "      clientToken: '" + config.clientToken + "',\n" +
        "      applicationId: '" + config.applicationId + "',\n" +
        "      site: '" + config.site + "',\n" +
        "      service: '" + config.service + "',\n" +
        "      env: '" + config.env + "',\n" +
        "      version: '" + config.version + "',\n" +
        "      sessionSampleRate: " + config.sessionSampleRate + ",\n" +
        "      sessionReplaySampleRate: " + config.sessionReplaySampleRate + ",\n" +
        "      trackUserInteractions: true,\n" +
        "      trackResources: true,\n" +
        "      trackLongTasks: true,\n" +
        "      defaultPrivacyLevel: '" + config.defaultPrivacyLevel + "'\n" +
        "    });\n" +
        "  })\n" +
        "</script>";
  }

  static String getStringConfig(String filterName, Function<String, String> getInitParameter, String key,
      String defaultValue, Consumer<String> logger) {
    String initParameter = getInitParameter.apply(key);
    if (initParameter == null || (initParameter.isEmpty() && !initParameter.equals(defaultValue))) {
      if (defaultValue == null) {
        logger.accept("Missing init-param " + key + " for filter-class " + filterName);
        throw new IllegalStateException("Missing init-param " + key);
      } else {
        if (!DEFAULT_RATE.equals(defaultValue)) {
          logger.accept("Missing init-param " + key + " for filter-class " + filterName + ", using default value "
              + defaultValue);
        }
        initParameter = defaultValue;
      }
    }
    return initParameter;
  }

  static int getRateConfig(String filterName, Function<String, String> getInitParameter, String key, int defaultRate,
      Consumer<String> logger) {
    String rateString = getStringConfig(filterName, getInitParameter, key, DEFAULT_RATE, logger);
    if (DEFAULT_RATE.equals(rateString)) {
      return defaultRate;
    } else {
      try {
        int rate = Integer.parseInt(rateString);
        if (rate < 0 || rate > 100) {
          throw new IllegalStateException("Invalid to parse init-param " + key + " rate value: " + rateString
              + " (must be between 0 and 100 included");
        }
        return rate;
      } catch (NumberFormatException e) {
        logger.accept("Failed to parse init-param " + key + " for filter-class " + filterName
            + " as a rate. Using default: " + defaultRate);
        return defaultRate;
      }
    }
  }

}
