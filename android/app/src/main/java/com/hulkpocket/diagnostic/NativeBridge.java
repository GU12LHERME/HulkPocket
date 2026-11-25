package com.hulkpocket.diagnostic;

final class NativeBridge {
    private NativeBridge() {}

    static String check() {
        try {
            System.loadLibrary("hulkpocket_bridge");
            return MainActivity.nativeBridgeStatus();
        } catch (UnsatisfiedLinkError | SecurityException error) {
            return "Falha ao carregar a ponte ARM64: " + error.getMessage();
        }
    }
}
