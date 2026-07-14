package com.example.flashmaplab.probe;

import org.springframework.util.Assert;
import org.springframework.web.servlet.FlashMap;
import org.springframework.web.servlet.FlashMapManager;

import javax.servlet.http.HttpServletRequest;
import javax.servlet.http.HttpServletResponse;

/**
 * Benign decorator used only for local runtime-observability teaching.
 * It adds one diagnostic response header and delegates all Spring behavior
 * to the exact FlashMapManager instance that was installed previously.
 */
public final class DecoratingFlashMapManager implements FlashMapManager {

    public static final String PROBE_HEADER = "X-Lab-Probe";
    public static final String PROBE_VALUE = "flashmap";
    public static final String HIT_HEADER = "X-FlashMap-Probe";
    public static final String HIT_VALUE = "hit";

    private final FlashMapManager original;

    public DecoratingFlashMapManager(FlashMapManager original) {
        Assert.notNull(original, "original FlashMapManager must not be null");
        this.original = original;
    }

    public FlashMapManager getOriginal() {
        return this.original;
    }

    @Override
    public FlashMap retrieveAndUpdate(
            HttpServletRequest request, HttpServletResponse response) {

        if (PROBE_VALUE.equals(request.getHeader(PROBE_HEADER))) {
            response.setHeader(HIT_HEADER, HIT_VALUE);
        }
        return this.original.retrieveAndUpdate(request, response);
    }

    @Override
    public void saveOutputFlashMap(
            FlashMap flashMap,
            HttpServletRequest request,
            HttpServletResponse response) {

        this.original.saveOutputFlashMap(flashMap, request, response);
    }
}
