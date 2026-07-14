package com.example.flashmaplab.probe;

import org.springframework.context.ApplicationContext;
import org.springframework.web.context.WebApplicationContext;
import org.springframework.web.context.support.WebApplicationContextUtils;
import org.springframework.web.servlet.DispatcherServlet;
import org.springframework.web.servlet.FlashMapManager;
import org.springframework.web.servlet.FrameworkServlet;

import javax.servlet.ServletContext;
import javax.servlet.http.HttpServletRequest;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Enumeration;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Set;

/**
 * Request-triggered installer for the local JSP diagnostic page.
 *
 * This class does not register a Filter or Servlet. It only locates the
 * already-live DispatcherServlet bean in this one local Boot web application
 * and decorates its existing FlashMapManager reference.
 */
public final class ProbeInstaller {

    private static final String DISPATCHER_SERVLET_BEAN_NAME = "dispatcherServlet";
    private static final String FLASH_MAP_MANAGER_FIELD = "flashMapManager";

    private ProbeInstaller() {
    }

    public static String install(HttpServletRequest request) {
        StringBuilder log = new StringBuilder(1024);
        log.append("action=install\n");

        DispatcherServlet dispatcherServlet = locateDispatcherServlet(request, log);
        if (dispatcherServlet == null) {
            log.append("result=FAILED: no live DispatcherServlet was found\n");
            appendWarmupHint(log);
            return log.toString();
        }

        log.append("dispatcherServlet=")
                .append(describe(dispatcherServlet))
                .append('\n');

        synchronized (dispatcherServlet) {
            try {
                Field field = findField(dispatcherServlet.getClass(), FLASH_MAP_MANAGER_FIELD);
                if (field == null) {
                    log.append("result=FAILED: field flashMapManager was not found\n");
                    return log.toString();
                }
                field.setAccessible(true);

                Object current = field.get(dispatcherServlet);
                log.append("currentFlashMapManager=").append(describe(current)).append('\n');

                if (current == null) {
                    log.append("result=FAILED: flashMapManager is null; DispatcherServlet is not initialized\n");
                    appendWarmupHint(log);
                    return log.toString();
                }
                if (current instanceof DecoratingFlashMapManager) {
                    DecoratingFlashMapManager installed = (DecoratingFlashMapManager) current;
                    log.append("result=OK: already installed (idempotent)\n")
                            .append("original=")
                            .append(describe(installed.getOriginal()))
                            .append('\n');
                    return log.toString();
                }
                if (!(current instanceof FlashMapManager)) {
                    log.append("result=FAILED: field value does not implement FlashMapManager\n");
                    return log.toString();
                }

                FlashMapManager original = (FlashMapManager) current;
                DecoratingFlashMapManager decorator =
                        new DecoratingFlashMapManager(original);
                field.set(dispatcherServlet, decorator);

                Object after = field.get(dispatcherServlet);
                if (after != decorator) {
                    log.append("result=FAILED: reflective write could not be verified\n");
                    return log.toString();
                }

                log.append("newFlashMapManager=").append(describe(after)).append('\n')
                        .append("originalPreserved=").append(describe(original)).append('\n')
                        .append("result=OK: decorator installed\n")
                        .append("probe=X-Lab-Probe: flashmap -> X-FlashMap-Probe: hit\n");
            }
            catch (RuntimeException ex) {
                log.append("result=FAILED: ")
                        .append(ex.getClass().getName())
                        .append(": ")
                        .append(safeMessage(ex))
                        .append('\n');
            }
            catch (Exception ex) {
                log.append("result=FAILED: ")
                        .append(ex.getClass().getName())
                        .append(": ")
                        .append(safeMessage(ex))
                        .append('\n');
            }
        }
        return log.toString();
    }

    public static String status(HttpServletRequest request) {
        StringBuilder log = new StringBuilder(768);
        log.append("action=status\n");

        DispatcherServlet dispatcherServlet = locateDispatcherServlet(request, log);
        if (dispatcherServlet == null) {
            log.append("installed=false\n")
                    .append("result=FAILED: no live DispatcherServlet was found\n");
            appendWarmupHint(log);
            return log.toString();
        }

        log.append("dispatcherServlet=")
                .append(describe(dispatcherServlet))
                .append('\n');

        try {
            Field field = findField(dispatcherServlet.getClass(), FLASH_MAP_MANAGER_FIELD);
            if (field == null) {
                log.append("installed=false\n")
                        .append("result=FAILED: field flashMapManager was not found\n");
                return log.toString();
            }
            field.setAccessible(true);
            Object current = field.get(dispatcherServlet);
            log.append("currentFlashMapManager=").append(describe(current)).append('\n');

            if (current instanceof DecoratingFlashMapManager) {
                DecoratingFlashMapManager installed = (DecoratingFlashMapManager) current;
                log.append("installed=true\n")
                        .append("original=")
                        .append(describe(installed.getOriginal()))
                        .append('\n');
            }
            else {
                log.append("installed=false\n");
                if (current == null) {
                    appendWarmupHint(log);
                }
            }
        }
        catch (Exception ex) {
            log.append("installed=unknown\n")
                    .append("result=FAILED: ")
                    .append(ex.getClass().getName())
                    .append(": ")
                    .append(safeMessage(ex))
                    .append('\n');
        }
        return log.toString();
    }

    public static String help() {
        return "action=help\n"
                + "GET /diag/install-probe.jsp?action=install\n"
                + "GET /diag/install-probe.jsp?action=status\n"
                + "GET /diag/install-probe.jsp?action=help\n"
                + "GET /hello\n"
                + "Probe request header: X-Lab-Probe: flashmap\n"
                + "Expected response header: X-FlashMap-Probe: hit\n";
    }

    private static DispatcherServlet locateDispatcherServlet(
            HttpServletRequest request, StringBuilder log) {

        if (request == null) {
            log.append("locate.request=FAILED: request is null\n");
            return null;
        }

        log.append("locate.request=OK: current JSP request is available\n");
        List<ApplicationContext> contexts = new ArrayList<ApplicationContext>();
        Set<ApplicationContext> seen =
                Collections.newSetFromMap(new IdentityHashMap<ApplicationContext, Boolean>());

        Object requestContext = request.getAttribute(
                DispatcherServlet.WEB_APPLICATION_CONTEXT_ATTRIBUTE);
        if (requestContext instanceof ApplicationContext) {
            log.append("locate.requestAttribute=OK: ")
                    .append(describe(requestContext))
                    .append('\n');
            addContext((ApplicationContext) requestContext, contexts, seen);
        }
        else {
            log.append("locate.requestAttribute=MISS: direct JSP requests normally do not pass through DispatcherServlet\n");
        }

        ServletContext servletContext = request.getServletContext();
        if (servletContext == null) {
            log.append("locate.servletContext=FAILED: request.getServletContext() returned null\n");
        }
        else {
            log.append("locate.servletContext=OK: contextPath=")
                    .append(servletContext.getContextPath())
                    .append('\n');

            WebApplicationContext root =
                    WebApplicationContextUtils.getWebApplicationContext(servletContext);
            if (root != null) {
                log.append("locate.rootWac=OK: id=")
                        .append(root.getId())
                        .append('\n');
                addContext(root, contexts, seen);
            }
            else {
                log.append("locate.rootWac=MISS\n");
            }

            Enumeration<String> names = servletContext.getAttributeNames();
            while (names.hasMoreElements()) {
                String name = names.nextElement();
                if (!name.startsWith(FrameworkServlet.SERVLET_CONTEXT_PREFIX)) {
                    continue;
                }
                Object value = servletContext.getAttribute(name);
                if (value instanceof ApplicationContext) {
                    log.append("locate.frameworkServletWac=OK: attribute=")
                            .append(name)
                            .append('\n');
                    addContext((ApplicationContext) value, contexts, seen);
                }
            }
        }

        for (ApplicationContext context : contexts) {
            DispatcherServlet found = findDispatcherServletBean(context, log);
            if (found != null) {
                return found;
            }
        }

        log.append("locate.bean=MISS: checkedContexts=")
                .append(contexts.size())
                .append('\n');
        return null;
    }

    private static DispatcherServlet findDispatcherServletBean(
            ApplicationContext context, StringBuilder log) {

        Set<ApplicationContext> visited =
                Collections.newSetFromMap(new IdentityHashMap<ApplicationContext, Boolean>());
        ApplicationContext current = context;

        while (current != null && visited.add(current)) {
            log.append("locate.inspectWac=id=")
                    .append(current.getId())
                    .append(", type=")
                    .append(current.getClass().getName())
                    .append('\n');

            try {
                if (current.containsBean(DISPATCHER_SERVLET_BEAN_NAME)) {
                    Object named = current.getBean(DISPATCHER_SERVLET_BEAN_NAME);
                    if (named instanceof DispatcherServlet) {
                        log.append("locate.bean=OK: name=dispatcherServlet\n");
                        return (DispatcherServlet) named;
                    }
                    log.append("locate.namedBean=MISS: dispatcherServlet has type ")
                            .append(describe(named))
                            .append('\n');
                }

                String[] beanNames = current.getBeanNamesForType(
                        DispatcherServlet.class, true, false);
                for (String beanName : beanNames) {
                    Object candidate = current.getBean(beanName);
                    if (candidate instanceof DispatcherServlet) {
                        log.append("locate.bean=OK: typeLookupName=")
                                .append(beanName)
                                .append('\n');
                        return (DispatcherServlet) candidate;
                    }
                }
            }
            catch (RuntimeException ex) {
                log.append("locate.inspectWac=FAILED: ")
                        .append(ex.getClass().getName())
                        .append(": ")
                        .append(safeMessage(ex))
                        .append('\n');
            }
            current = current.getParent();
        }
        return null;
    }

    private static void addContext(
            ApplicationContext context,
            List<ApplicationContext> contexts,
            Set<ApplicationContext> seen) {

        if (context != null && seen.add(context)) {
            contexts.add(context);
        }
    }

    private static Field findField(Class<?> type, String name) {
        Class<?> current = type;
        while (current != null && current != Object.class) {
            try {
                return current.getDeclaredField(name);
            }
            catch (NoSuchFieldException ignored) {
                current = current.getSuperclass();
            }
        }
        return null;
    }

    private static String describe(Object value) {
        if (value == null) {
            return "null";
        }
        return value.getClass().getName()
                + "@"
                + Integer.toHexString(System.identityHashCode(value));
    }

    private static String safeMessage(Throwable throwable) {
        String message = throwable.getMessage();
        return message == null ? "(no message)" : message.replace('\n', ' ');
    }

    private static void appendWarmupHint(StringBuilder log) {
        log.append("hint=Keep spring.mvc.servlet.load-on-startup=1, or GET /hello once before install\n");
    }
}
