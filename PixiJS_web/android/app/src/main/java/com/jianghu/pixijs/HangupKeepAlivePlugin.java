package com.jianghu.pixijs;

import com.getcapacitor.Plugin;
import com.getcapacitor.PluginCall;
import com.getcapacitor.PluginMethod;
import com.getcapacitor.annotation.CapacitorPlugin;

/** JS bridge: start/stop hangup foreground keepalive while AFK. */
@CapacitorPlugin(name = "HangupKeepAlive")
public class HangupKeepAlivePlugin extends Plugin {
    @PluginMethod
    public void start(PluginCall call) {
        try {
            HangupKeepAliveService.start(getContext().getApplicationContext());
            call.resolve();
        } catch (Exception e) {
            call.reject("hangup keepalive start failed", e);
        }
    }

    @PluginMethod
    public void stop(PluginCall call) {
        try {
            HangupKeepAliveService.stop(getContext().getApplicationContext());
            call.resolve();
        } catch (Exception e) {
            call.reject("hangup keepalive stop failed", e);
        }
    }
}
