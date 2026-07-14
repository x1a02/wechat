<%@ page contentType="text/plain;charset=UTF-8" pageEncoding="UTF-8" %>
<%@ page import="com.example.flashmaplab.probe.ProbeInstaller" %>
<%
    String action = request.getParameter("action");
    if (action == null || action.length() == 0) {
        action = "help";
    }

    if ("install".equals(action)) {
        out.print(ProbeInstaller.install(request));
    }
    else if ("status".equals(action)) {
        out.print(ProbeInstaller.status(request));
    }
    else if ("help".equals(action)) {
        out.print(ProbeInstaller.help());
    }
    else {
        response.setStatus(400);
        out.print("result=FAILED: unsupported action\n");
        out.print(ProbeInstaller.help());
    }
%>
