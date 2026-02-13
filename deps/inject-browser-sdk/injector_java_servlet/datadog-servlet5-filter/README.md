# DatadogBrowserFilter

MVP of a Java Servlet Filter that modifies HTML servlet responses.

## Requirements

* Java 17+
* Servlet API 5.0-6.0
* Maven 3.9.6

## Compatibility

* Apache Tomcat 10.0.x-10.1.x ([compatibility reference](https://tomcat.apache.org/whichversion.html))

## Usage

Build the filter JAR using Maven, and copy the `java-servlet5-filter-<version>-jar-with-dependencies.jar` into the `lib` folder of your Tomcat installation:
```shell
./mvn package
cp datadog-servlet5-filter-<version>-jar-with-dependencies.jar <tomcat-install-dir>/lib/
```
Then map the filter and configure the RUM SDK using the `web.xml` configuration file:

```xml
  <web-app>
    ...
    
    <filter>
      <filter-name>DatadogBrowserFilter</filter-name>
      <filter-class>com.datadog.rum.browser.DatadogBrowserFilter</filter-class>
      <async-supported>true</async-supported>
      
      <init-param>
        <param-name>client-token</param-name>
        <param-value><!-- your client token --></param-value>
      </init-param>
      <init-param>
        <param-name>application-id</param-name>
        <param-value><!-- your application id --></param-value>
      </init-param>
      <init-param>
        <param-name>site</param-name>
        <param-value><!-- site, "datadoghq.com" by default --></param-value>
      </init-param>
      <init-param>
        <param-name>service</param-name>
        <param-value><!-- your service name (optional) --></param-value>
      </init-param>
      <init-param>
        <param-name>env</param-name>
        <param-value><!-- your environment name (optional) e.g: 'staging-1' or 'prod' --></param-value>
      </init-param>
      <init-param>
        <param-name>version</param-name>
        <param-value><!-- your application version (optional) --></param-value>
      </init-param>
      <init-param>
        <param-name>session-sample-rate</param-name>
        <param-value><!-- sample rate in percent, 100 by default --></param-value>
      </init-param>
      <init-param>
        <param-name>session-replay-sample-rate</param-name>
        <param-value><!-- Session Replay's sample rate in percent, 100 by default --></param-value>
      </init-param>
      <init-param>
        <param-name>default-privacy-level</param-name>
        <param-value><!-- Session Replay's default privacy level, 'mask-user-input' by default --></param-value>
      </init-param>
    </filter>
    
    <filter-mapping>
      <filter-name>DatadogBrowserFilter</filter-name>
      <url-pattern>/*</url-pattern>
      <dispatcher>ERROR</dispatcher>
      <dispatcher>REQUEST</dispatcher>
    </filter-mapping>
    
    ...
  </web-app>
```
