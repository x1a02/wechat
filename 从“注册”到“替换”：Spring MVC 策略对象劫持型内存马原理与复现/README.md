# 从“注册”到“替换”：Spring MVC 策略对象劫持型内存马原理与复现

> 本文配套代码：FlashMapManager 装饰器本地实验（策略对象劫持型内存马原理复现）

## A. 架构说明

这是一个本地、无害的 Spring MVC 运行时可观测性实验：

```text
GET /diag/install-probe.jsp?action=install
  → JSP 持有当前 HttpServletRequest
  → ServletContext 取得 Boot 根 WebApplicationContext
  → 查找 dispatcherServlet Bean
  → 读取 DispatcherServlet.flashMapManager
  → 安装 DecoratingFlashMapManager(original)

GET /hello + X-Lab-Probe: flashmap
  → DispatcherServlet.doService
  → DecoratingFlashMapManager.retrieveAndUpdate
  → 增加 X-FlashMap-Probe: hit
  → 委托替换前的 original FlashMapManager
  → HelloController 返回 hello
```

它没有向 Filter、Servlet 或 Listener 注册表增加组件，也没有使用启动监听器代替 JSP 触发。装饰器不执行系统命令、不连接外部主机、不下载文件。

## B. 环境选择

- Spring Boot `2.7.18`，对应 Spring Framework `5.3.31` 和 `javax.servlet`。
- `war` 打包：Spring Boot 的 executable jar 不支持 JSP，executable war 可以通过 `java -jar` 启动并解析 `src/main/webapp` 中的 JSP。
- 编译目标 Java 8；JDK 8 和 JDK 11 都足够。教学环境推荐 JDK 11，当前工程也可在兼容的更高 JDK 上运行。
- 地址固定为 `127.0.0.1:18080`。
- JSP 固定为 `/diag/install-probe.jsp`，业务路径固定为 `/hello`。
- `spring.mvc.servlet.load-on-startup=1` 保证 DispatcherServlet 及其策略在直接访问 JSP 前完成初始化。

## C. 构建与启动

在本目录下执行：

```bash
mvn clean package
java -jar target/flashmap-decorator-jsp-demo.war
```

看到类似日志即表示启动成功：

```text
Tomcat started on port(s): 18080 (http)
Started FlashMapDecoratorJspDemoApplication
```

## D. 验证命令与期望输出

### 1. 查看帮助

```bash
curl --noproxy '*' 'http://127.0.0.1:18080/diag/install-probe.jsp?action=help'
```

期望包含：

```text
action=help
GET /diag/install-probe.jsp?action=install
```

### 2. 安装前查看状态

```bash
curl --noproxy '*' 'http://127.0.0.1:18080/diag/install-probe.jsp?action=status'
```

期望关键行：

```text
currentFlashMapManager=org.springframework.web.servlet.support.SessionFlashMapManager@...
installed=false
```

### 3. 从 JSP 安装装饰器

```bash
curl --noproxy '*' 'http://127.0.0.1:18080/diag/install-probe.jsp?action=install'
```

期望关键行：

```text
currentFlashMapManager=org.springframework.web.servlet.support.SessionFlashMapManager@...
newFlashMapManager=com.example.flashmaplab.probe.DecoratingFlashMapManager@...
originalPreserved=org.springframework.web.servlet.support.SessionFlashMapManager@...
result=OK: decorator installed
```

重复执行同一命令时应返回：

```text
result=OK: already installed (idempotent)
```

### 4. 普通业务请求

```bash
curl --noproxy '*' -i 'http://127.0.0.1:18080/hello'
```

期望 body 正常，且没有 `X-FlashMap-Probe`：

```text
HTTP/1.1 200

hello
```

### 5. 带诊断 Header 的业务请求

```bash
curl --noproxy '*' -i \
  -H 'X-Lab-Probe: flashmap' \
  'http://127.0.0.1:18080/hello'
```

期望：

```text
HTTP/1.1 200
X-FlashMap-Probe: hit

hello
```

## E. 定位优先级

1. 检查 `DispatcherServlet.WEB_APPLICATION_CONTEXT_ATTRIBUTE`。直接 JSP 请求通常不会经过 DispatcherServlet，因此这里出现 `MISS` 是正常现象。
2. 从 `request.getServletContext()` 取得 Boot 根 `WebApplicationContext`。
3. 枚举 `FrameworkServlet.SERVLET_CONTEXT_PREFIX` 开头的 ServletContext 属性，补充可能的 Servlet 子上下文。
4. 在收集到的 Context 及其父 Context 中，优先查找名为 `dispatcherServlet` 的 Bean，再按类型查找。

本 Demo 是单 Boot WebApplicationContext 教学环境，不实现传统多 WAR/Tomcat `StandardWrapper.instance` 的跨 ClassLoader 搜索。

## F. 常见失败排查

### JSP 返回 404

- 必须使用 `war` 打包并通过 `java -jar target/flashmap-decorator-jsp-demo.war` 启动。
- 确认文件位于 `src/main/webapp/diag/install-probe.jsp`。
- 确认 `tomcat-embed-jasper` 依赖存在。
- 不要改为普通 executable jar；Boot 2.7 的 executable jar 不支持 JSP。

### 启动时报 `Port 18080 is already in use`

先确认占用进程是否属于其他本地实验，不要直接终止未知进程：

```bash
lsof -nP -iTCP:18080 -sTCP:LISTEN
```

项目默认值仍固定为 `18080`。如果只需临时并行验证，可在启动时覆盖端口，并把后续 curl 中的端口同步改掉：

```bash
java -jar target/flashmap-decorator-jsp-demo.war --server.port=18081
```

### JSP 被当作下载或源码文本

说明 Jasper 没有生效。检查 `tomcat-embed-jasper` 是否进入 `WEB-INF/lib-provided`，并确认启动的是重新构建后的 WAR。

### `locate.rootWac=MISS`

- 确认应用通过 `FlashMapDecoratorJspDemoApplication` 启动。
- 确认请求访问的是本应用的 `127.0.0.1:18080`，没有额外 context path。
- 如果部署到外部容器，确认 JSP 与 Spring 应用位于同一个 WAR。

### `locate.bean=MISS`

- 确认 `spring-boot-starter-web` 已加载，启动日志中存在 DispatcherServlet。
- 本定位方案针对 Boot 2.7 单应用；传统 `web.xml` WAR 中 DispatcherServlet 可能是容器实例而不是 Spring Bean，需使用容器图定位，超出本最小实验范围。

### `flashMapManager is null`

DispatcherServlet 尚未初始化。默认配置已设置：

```properties
spring.mvc.servlet.load-on-startup=1
```

如果删除该配置，先执行一次：

```bash
curl --noproxy '*' 'http://127.0.0.1:18080/hello'
```

再访问安装 JSP。安装器不会在字段为 `null` 时强行调用 Spring 私有初始化方法。

### 反射访问失败

本工程运行在普通 classpath/unnamed module 下，JDK 8/11 不需要额外 `--add-opens`。如果改造成显式 JPMS named module，需要由模块配置开放 Spring 所在包；不要把 `--add-opens java.base/java.lang` 当成本实验的必要条件。

### 安装成功但业务响应没有标记

- 确认请求访问 `/hello`，而不是 JSP 本身；JSP 请求由 Tomcat `JspServlet` 处理，不走 DispatcherServlet 的 FlashMapManager。
- Header 必须精确为 `X-Lab-Probe: flashmap`。
- 再次访问 `action=status`，确认 `installed=true`。
- 确认没有重启或重新部署应用；重启后内存中的字段会恢复默认值。
