package com.jianghu.pixijs;

import com.getcapacitor.Plugin;
import com.getcapacitor.PluginCall;
import com.getcapacitor.PluginMethod;
import com.getcapacitor.annotation.CapacitorPlugin;

/** JS bridge: start/stop foreground keepalive (in-world or hangup). */
@CapacitorPlugin(name = "HangupKeepAlive")
public class HangupKeepAlivePlugin extends Plugin {
    @PluginMethod
    public void start(PluginCall call) {
        try {
            String mode = call.getString("mode", HangupKeepAliveService.MODE_HANGUP);
            HangupKeepAliveService.start(getContext().getApplicationContext(), mode);
            call.resolve();
        } catch (Exception e) {
            call.reject("keepalive start failed", e);
        }
    }

    @PluginMethod
    public void stop(PluginCall call) {
        try {
            HangupKeepAliveService.stop(getContext().getApplicationContext());
            call.resolve();
        } catch (Exception e) {
            call.reject("keepalive stop failed", e);
        }
    }
}
