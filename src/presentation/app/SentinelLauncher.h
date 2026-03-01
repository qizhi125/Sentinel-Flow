#pragma once

class SentinelLauncher {
public:
    static int run(int argc, char *argv[]);

private:
    static void handleSignal(int sig);
};